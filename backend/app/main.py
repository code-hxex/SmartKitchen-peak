import asyncio
import os
from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone

from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from sqlalchemy import select

from . import config
from .database import Base, SessionLocal, engine
from .models import Alert, FridgeItem
from .routers import alerts, door, events, items, reconcile, registration, trajectory

templates = Jinja2Templates(directory=os.path.join(os.path.dirname(os.path.abspath(__file__)), "templates"))


async def _periodic_reconcile_check():
    """일정 주기로, 오래 안 보인(last_seen_at) in_fridge 항목에 '재점검 필요' 알림을 남긴다.
    풀스캔 입력이 없어도 동작하는 최소한의 DB 위생 점검 - 실제 전체 재대조는
    /api/reconcile 로 외부에서 스캔 결과를 보냈을 때 이루어진다."""
    while True:
        await asyncio.sleep(config.RECONCILE_INTERVAL_MINUTES * 60)
        db = SessionLocal()
        try:
            cutoff = datetime.now(timezone.utc) - timedelta(hours=config.STALE_HOURS_THRESHOLD)
            stale_items = (
                db.execute(
                    select(FridgeItem)
                    .where(FridgeItem.status == "in_fridge")
                    .where(FridgeItem.last_seen_at < cutoff)
                )
                .scalars()
                .all()
            )
            for item in stale_items:
                already_alerted = (
                    db.execute(
                        select(Alert)
                        .where(Alert.alert_type == "reconcile_stale")
                        .where(Alert.item_id == item.item_id)
                        .where(Alert.resolved.is_(False))
                    )
                    .scalars()
                    .first()
                )
                if already_alerted:
                    continue
                db.add(
                    Alert(
                        alert_type="reconcile_stale",
                        severity="info",
                        message=(
                            f"'{item.label}'이(가) {config.STALE_HOURS_THRESHOLD}시간 이상 "
                            "감지되지 않았습니다. 실물 재확인이 필요할 수 있습니다."
                        ),
                        item_id=item.item_id,
                    )
                )
            db.commit()
        except Exception as e:
            print(f"[reconcile] 주기 점검 중 오류: {e}")
        finally:
            db.close()


@asynccontextmanager
async def lifespan(app: FastAPI):
    Base.metadata.create_all(bind=engine)
    db = SessionLocal()
    try:
        door.hydrate_from_db(db)
    finally:
        db.close()
    door.set_main_loop(asyncio.get_running_loop())
    task = asyncio.create_task(_periodic_reconcile_check())
    yield
    task.cancel()


app = FastAPI(title="냉장고 재고 트래킹 백엔드", lifespan=lifespan)

# Flask 프론트(127.0.0.1:5000)가 문 상태 SSE(/api/door/stream)를 직접 구독할 수 있게 허용.
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://127.0.0.1:5000", "http://localhost:5000"],
    allow_methods=["GET", "POST"],
    allow_headers=["*"],
)

app.include_router(events.router)
app.include_router(trajectory.router)
app.include_router(registration.router)
app.include_router(reconcile.router)
app.include_router(items.router)
app.include_router(alerts.router)
app.include_router(door.router)

os.makedirs(config.REGISTRATION_FRAME_DIR, exist_ok=True)
app.mount(
    "/static/registration_frames",
    StaticFiles(directory=config.REGISTRATION_FRAME_DIR),
    name="registration_frames",
)


@app.get("/")
def root():
    return {"status": "ok", "service": "fridge-inventory-backend"}


@app.get("/dashboard")
def dashboard(request: Request):
    return templates.TemplateResponse(
        request, "dashboard.html", {"base_url": str(request.base_url)}
    )
