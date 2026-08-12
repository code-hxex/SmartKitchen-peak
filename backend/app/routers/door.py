"""리드스위치 기반 냉장고 문 개폐 상태 API.

엣지 디바이스(ESP32)가 문 상태가 바뀔 때마다 WiFi로 POST한다. 프론트엔드는
GET /api/door 로 현재 상태를 한 번 조회하거나, GET /api/door/stream (SSE)으로
구독하면 센서값이 갱신될 때마다(POST가 들어올 때마다) 즉시 push를 받는다.
최신 상태는 메모리에 캐시해 두고, 상태가 "바뀔 때"만 alerts 테이블에 기록해
재시작 후에도 마지막 상태를 복원할 수 있게 한다.
"""

import asyncio
import threading
from datetime import datetime, timezone
from typing import AsyncIterator

from fastapi import APIRouter, Depends
from fastapi.responses import StreamingResponse
from sqlalchemy import select
from sqlalchemy.orm import Session

from .. import schemas
from ..database import get_db
from ..models import Alert

router = APIRouter(prefix="/api/door", tags=["door"])

_lock = threading.Lock()
_state = {"is_open": None, "device_id": None, "updated_at": None}
_subscribers: set[asyncio.Queue] = set()
_main_loop: asyncio.AbstractEventLoop | None = None


def set_main_loop(loop: asyncio.AbstractEventLoop):
    """POST 핸들러가 스레드풀에서 돌아도 안전하게 SSE 큐에 넣을 수 있도록,
    앱 시작 시(이벤트 루프가 도는 스레드에서) 메인 루프 참조를 저장해 둔다."""
    global _main_loop
    _main_loop = loop


def _current_out() -> schemas.DoorStatusOut:
    with _lock:
        return schemas.DoorStatusOut(**_state)


def hydrate_from_db(db: Session):
    """백엔드 재시작 시 마지막으로 기록된 문 상태를 alerts 로그에서 복원한다."""
    row = (
        db.execute(
            select(Alert)
            .where(Alert.alert_type.in_(["door_open", "door_closed"]))
            .order_by(Alert.created_at.desc())
        )
        .scalars()
        .first()
    )
    if row:
        with _lock:
            _state["is_open"] = row.alert_type == "door_open"
            _state["device_id"] = row.device_id
            _state["updated_at"] = row.created_at


def _broadcast(payload_json: str):
    if _main_loop is None:
        return
    for queue in list(_subscribers):
        _main_loop.call_soon_threadsafe(queue.put_nowait, payload_json)


@router.get("", response_model=schemas.DoorStatusOut)
def get_door_status():
    return _current_out()


@router.get("/stream")
async def stream_door_status() -> StreamingResponse:
    """SSE 구독. 연결 즉시 현재 상태를 한 번 보내고, 이후 상태가 갱신될 때마다 push한다."""
    queue: asyncio.Queue = asyncio.Queue()
    _subscribers.add(queue)

    async def event_gen() -> AsyncIterator[str]:
        try:
            yield f"data: {_current_out().model_dump_json()}\n\n"
            while True:
                payload_json = await queue.get()
                yield f"data: {payload_json}\n\n"
        finally:
            _subscribers.discard(queue)

    return StreamingResponse(
        event_gen(),
        media_type="text/event-stream",
        headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"},
    )


@router.post("", response_model=schemas.DoorStatusOut)
def report_door_status(payload: schemas.DoorStatusIn, db: Session = Depends(get_db)):
    now = datetime.now(timezone.utc)
    with _lock:
        changed = _state["is_open"] != payload.is_open
        _state["is_open"] = payload.is_open
        _state["device_id"] = payload.device_id
        _state["updated_at"] = now
        result = schemas.DoorStatusOut(**_state)

    if changed:
        db.add(
            Alert(
                alert_type="door_open" if payload.is_open else "door_closed",
                severity="info",
                message=f"냉장고 문이 {'열렸습니다' if payload.is_open else '닫혔습니다'}",
                device_id=payload.device_id,
            )
        )
        db.commit()

    _broadcast(result.model_dump_json())
    return result
