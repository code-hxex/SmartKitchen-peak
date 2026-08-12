import uuid
from datetime import datetime, timezone

from sqlalchemy import Boolean, DateTime, Float, ForeignKey, Integer, JSON, String
from sqlalchemy.orm import Mapped, mapped_column

from .database import Base


def _now() -> datetime:
    return datetime.now(timezone.utc)


def _new_item_id() -> str:
    return str(uuid.uuid4())


class FridgeItem(Base):
    """'현재 상태' 테이블 - 냉장고 안에 있(었)던 개별 물건 하나."""

    __tablename__ = "fridge_items"

    item_id: Mapped[str] = mapped_column(String, primary_key=True, default=_new_item_id)
    class_name: Mapped[str] = mapped_column(String, index=True)  # 예: 유제품, 음료, 야채...
    label: Mapped[str] = mapped_column(String)  # 예: 서울우유 1L
    entry_date: Mapped[datetime] = mapped_column(DateTime, default=_now)  # 투입일
    status: Mapped[str] = mapped_column(String, default="in_fridge", index=True)  # in_fridge|removed|expired
    embedding_vector: Mapped[list | None] = mapped_column(JSON, nullable=True)
    confidence: Mapped[str | None] = mapped_column(String, nullable=True)  # low|medium|high
    removed_at: Mapped[datetime | None] = mapped_column(DateTime, nullable=True)
    last_seen_at: Mapped[datetime] = mapped_column(DateTime, default=_now)
    source: Mapped[str] = mapped_column(String, default="trajectory")  # trajectory|registration|reconcile
    created_at: Mapped[datetime] = mapped_column(DateTime, default=_now)
    updated_at: Mapped[datetime] = mapped_column(DateTime, default=_now, onupdate=_now)


class EdgeEvent(Base):
    """엣지 디바이스로부터 수신한 원시 이벤트(프레임/센서값) 로그."""

    __tablename__ = "edge_events"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[str] = mapped_column(String, index=True)
    event_type: Mapped[str] = mapped_column(String)  # frame|sensor|frame+sensor|empty
    frame_path: Mapped[str | None] = mapped_column(String, nullable=True)
    payload: Mapped[dict | None] = mapped_column(JSON, nullable=True)
    received_at: Mapped[datetime] = mapped_column(DateTime, default=_now)


class SensorReading(Base):
    """BME680 등 가스 센서 값."""

    __tablename__ = "sensor_readings"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    device_id: Mapped[str] = mapped_column(String, index=True)
    temperature_c: Mapped[float | None] = mapped_column(Float, nullable=True)
    humidity_pct: Mapped[float | None] = mapped_column(Float, nullable=True)
    pressure_hpa: Mapped[float | None] = mapped_column(Float, nullable=True)
    gas_resistance_ohm: Mapped[float | None] = mapped_column(Float, nullable=True)
    is_anomaly: Mapped[bool] = mapped_column(Boolean, default=False)
    baseline_avg_ohm: Mapped[float | None] = mapped_column(Float, nullable=True)
    recorded_at: Mapped[datetime] = mapped_column(DateTime, default=_now)


class Alert(Base):
    """가스 이상치, 재점검 필요, 반출 불일치 등 알림 로그."""

    __tablename__ = "alerts"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    alert_type: Mapped[str] = mapped_column(String, index=True)  # gas_anomaly|reconcile_stale|removal_unmatched
    severity: Mapped[str] = mapped_column(String, default="warning")  # info|warning|critical
    message: Mapped[str] = mapped_column(String)
    device_id: Mapped[str | None] = mapped_column(String, nullable=True)
    item_id: Mapped[str | None] = mapped_column(String, ForeignKey("fridge_items.item_id"), nullable=True)
    sensor_reading_id: Mapped[int | None] = mapped_column(
        Integer, ForeignKey("sensor_readings.id"), nullable=True
    )
    resolved: Mapped[bool] = mapped_column(Boolean, default=False)
    created_at: Mapped[datetime] = mapped_column(DateTime, default=_now)
