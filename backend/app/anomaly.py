"""BME680 가스(VOC) 저항값 기반 이상치 감지.

BME680은 공기 중 VOC 농도가 높아질수록 gas_resistance_ohm 값이 낮아진다.
최근 N개 판독값의 이동평균 대비 현재 값이 일정 비율 이상 떨어지면
이상치(예: 부패로 인한 냄새/VOC 급증 의심)로 판단한다.
"""

from sqlalchemy import select
from sqlalchemy.orm import Session

from . import config
from .models import SensorReading


def evaluate_gas_reading(
    db: Session, device_id: str, gas_resistance_ohm: float | None
) -> tuple[bool, float | None]:
    """(is_anomaly, baseline_avg) 튜플을 반환한다. 값이 없거나 기준선을 만들 데이터가
    부족하면 (False, None)을 반환한다."""
    if gas_resistance_ohm is None:
        return False, None

    recent = (
        db.execute(
            select(SensorReading.gas_resistance_ohm)
            .where(SensorReading.device_id == device_id)
            .where(SensorReading.gas_resistance_ohm.is_not(None))
            .order_by(SensorReading.recorded_at.desc())
            .limit(config.GAS_ANOMALY_WINDOW)
        )
        .scalars()
        .all()
    )

    if len(recent) < 5:
        return False, None

    baseline_avg = sum(recent) / len(recent)
    if baseline_avg <= 0:
        return False, baseline_avg

    drop_pct = (baseline_avg - gas_resistance_ohm) / baseline_avg * 100
    is_anomaly = drop_pct >= config.GAS_ANOMALY_DROP_PCT
    return is_anomaly, baseline_avg
