from datetime import datetime
from typing import Literal, Optional

from pydantic import BaseModel, ConfigDict


# ---------- 공통 ----------


class SensorReadingIn(BaseModel):
    temperature_c: Optional[float] = None
    humidity_pct: Optional[float] = None
    pressure_hpa: Optional[float] = None
    gas_resistance_ohm: Optional[float] = None


class SensorReadingOut(SensorReadingIn):
    model_config = ConfigDict(from_attributes=True)

    id: int
    device_id: str
    is_anomaly: bool
    baseline_avg_ohm: Optional[float]
    recorded_at: datetime


class AlertOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    alert_type: str
    severity: str
    message: str
    device_id: Optional[str]
    item_id: Optional[str]
    resolved: bool
    created_at: datetime


class FridgeItemOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    item_id: str
    class_name: str
    label: str
    entry_date: datetime
    status: str
    confidence: Optional[str]
    removed_at: Optional[datetime]
    last_seen_at: datetime
    source: str
    image_url: Optional[str] = None


# ---------- 냉장고 문 개폐 (리드스위치) ----------


class DoorStatusIn(BaseModel):
    device_id: str = "xiao-cam-01"
    is_open: bool


class DoorStatusOut(BaseModel):
    is_open: Optional[bool] = None
    device_id: Optional[str] = None
    updated_at: Optional[datetime] = None


# ---------- 이벤트 수신 (엣지 디바이스 -> 백엔드) ----------


class EdgeEventIn(BaseModel):
    device_id: str
    timestamp: Optional[datetime] = None
    frame_base64: Optional[str] = None  # JPEG base64 (선택)
    sensor: Optional[SensorReadingIn] = None


class EdgeEventOut(BaseModel):
    event_id: int
    device_id: str
    event_type: str
    frame_path: Optional[str]
    sensor_reading: Optional[SensorReadingOut] = None
    anomaly_alert: Optional[AlertOut] = None
    received_at: datetime


# ---------- 궤적 분석 결과 반영 ----------


class TrajectoryResultIn(BaseModel):
    device_id: str
    timestamp: Optional[datetime] = None
    direction: Literal["in", "out"]
    class_name: str
    label: str
    confidence: Literal["low", "medium", "high"] = "medium"
    embedding_vector: list[float]
    source_event_id: Optional[int] = None


class TrajectoryResultOut(BaseModel):
    action: Literal["created", "removed", "unmatched_removal"]
    item: Optional[FridgeItemOut]
    matched_similarity: Optional[float] = None


# ---------- 초기 등록 (스마트폰 촬영) ----------


class RegistrationIn(BaseModel):
    image_base64: str
    device_id: str = "smartphone"


class RegistrationOut(BaseModel):
    item: FridgeItemOut
    raw_recognition: dict


class ConfirmedItemIn(BaseModel):
    """이미 다른 곳(예: Flask 실시간 탐지)에서 Claude Vision으로 인식까지 끝낸
    물건을 그대로 등록할 때 사용 - 여기서는 Claude를 다시 호출하지 않는다."""

    device_id: str = "web-camera"
    class_name: str
    label: str
    confidence: Literal["low", "medium", "high"] = "medium"
    image_base64: str


# ---------- 주기적 전체 재점검(reconcile) ----------


class ReconcileScanItem(BaseModel):
    class_name: str
    label: str
    embedding_vector: list[float]
    confidence: Literal["low", "medium", "high"] = "medium"


class ReconcileReportIn(BaseModel):
    device_id: str
    scanned_items: list[ReconcileScanItem]


class ReconcileReportOut(BaseModel):
    matched: int
    newly_added: int
    marked_removed: int
    total_in_fridge: int
