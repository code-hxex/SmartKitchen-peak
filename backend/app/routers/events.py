"""이벤트 수신 API - 엣지 디바이스로부터 프레임/센서값을 받는다."""

import base64
import os
import uuid
from datetime import datetime, timezone

from fastapi import APIRouter, Depends
from sqlalchemy import select
from sqlalchemy.orm import Session

from .. import config, schemas
from ..anomaly import evaluate_gas_reading
from ..database import get_db
from ..models import Alert, EdgeEvent, SensorReading

router = APIRouter(prefix="/api/events", tags=["events"])


def _save_frame(device_id: str, jpeg_bytes: bytes) -> str:
    os.makedirs(config.EVENT_FRAME_DIR, exist_ok=True)
    filename = f"{device_id}_{uuid.uuid4().hex}.jpg"
    path = os.path.join(config.EVENT_FRAME_DIR, filename)
    with open(path, "wb") as f:
        f.write(jpeg_bytes)
    return path


@router.get("", response_model=list[schemas.EdgeEventOut])
def list_events(db: Session = Depends(get_db)):
    """최근 수신 이벤트 목록 - 실제 하드웨어가 보낸 이벤트가 도착했는지 확인용."""
    rows = (
        db.execute(select(EdgeEvent).order_by(EdgeEvent.received_at.desc()).limit(50))
        .scalars()
        .all()
    )
    return [
        schemas.EdgeEventOut(
            event_id=r.id,
            device_id=r.device_id,
            event_type=r.event_type,
            frame_path=r.frame_path,
            sensor_reading=None,
            anomaly_alert=None,
            received_at=r.received_at,
        )
        for r in rows
    ]


@router.post("", response_model=schemas.EdgeEventOut)
def receive_event(payload: schemas.EdgeEventIn, db: Session = Depends(get_db)):
    frame_path = None
    if payload.frame_base64:
        jpeg_bytes = base64.b64decode(payload.frame_base64)
        frame_path = _save_frame(payload.device_id, jpeg_bytes)

    event_type = "frame" if frame_path else ""
    if payload.sensor:
        event_type = (event_type + "+sensor").strip("+")
    if not event_type:
        event_type = "empty"

    received_at = payload.timestamp or datetime.now(timezone.utc)

    event = EdgeEvent(
        device_id=payload.device_id,
        event_type=event_type,
        frame_path=frame_path,
        payload=payload.sensor.model_dump() if payload.sensor else None,
        received_at=received_at,
    )
    db.add(event)
    db.flush()  # event.id 확보

    sensor_out = None
    alert_out = None
    if payload.sensor:
        is_anomaly, baseline = evaluate_gas_reading(
            db, payload.device_id, payload.sensor.gas_resistance_ohm
        )
        reading = SensorReading(
            device_id=payload.device_id,
            temperature_c=payload.sensor.temperature_c,
            humidity_pct=payload.sensor.humidity_pct,
            pressure_hpa=payload.sensor.pressure_hpa,
            gas_resistance_ohm=payload.sensor.gas_resistance_ohm,
            is_anomaly=is_anomaly,
            baseline_avg_ohm=baseline,
            recorded_at=received_at,
        )
        db.add(reading)
        db.flush()
        sensor_out = schemas.SensorReadingOut.model_validate(reading)

        if is_anomaly:
            alert = Alert(
                alert_type="gas_anomaly",
                severity="warning",
                message=(
                    f"{payload.device_id} 가스센서 이상치 감지: "
                    f"현재 {payload.sensor.gas_resistance_ohm:.0f}Ω, "
                    f"최근 평균 {baseline:.0f}Ω 대비 급락"
                ),
                device_id=payload.device_id,
                sensor_reading_id=reading.id,
            )
            db.add(alert)
            db.flush()
            alert_out = schemas.AlertOut.model_validate(alert)

    db.commit()

    return schemas.EdgeEventOut(
        event_id=event.id,
        device_id=event.device_id,
        event_type=event.event_type,
        frame_path=event.frame_path,
        sensor_reading=sensor_out,
        anomaly_alert=alert_out,
        received_at=event.received_at,
    )
