"""궤적 분석 결과 반영 API.

궤적 분석(카메라 영상에서 물건의 투입/반출 판단) 자체는 이 백엔드의 책임이
아니다 - 별도 CV/ML 컴포넌트가 계산한 결과(direction, class_name, label,
embedding_vector)를 받아서 '현재 상태' 테이블(fridge_items)에 반영하는
로직만 담당한다.
"""

from datetime import datetime, timezone

from fastapi import APIRouter, Depends
from sqlalchemy import select
from sqlalchemy.orm import Session

from .. import config, schemas
from ..database import get_db
from ..embeddings import cosine_similarity
from ..models import Alert, FridgeItem

router = APIRouter(prefix="/api/trajectory-result", tags=["trajectory"])


@router.post("", response_model=schemas.TrajectoryResultOut)
def apply_trajectory_result(payload: schemas.TrajectoryResultIn, db: Session = Depends(get_db)):
    now = payload.timestamp or datetime.now(timezone.utc)

    if payload.direction == "in":
        item = FridgeItem(
            class_name=payload.class_name,
            label=payload.label,
            entry_date=now,
            status="in_fridge",
            embedding_vector=payload.embedding_vector,
            confidence=payload.confidence,
            last_seen_at=now,
            source="trajectory",
        )
        db.add(item)
        db.commit()
        db.refresh(item)
        return schemas.TrajectoryResultOut(
            action="created", item=schemas.FridgeItemOut.model_validate(item)
        )

    # direction == "out": 임베딩 유사도로 어떤 물건이 나갔는지 매칭한다.
    candidates = (
        db.execute(
            select(FridgeItem)
            .where(FridgeItem.status == "in_fridge")
            .where(FridgeItem.class_name == payload.class_name)
        )
        .scalars()
        .all()
    )

    best_item, best_score = None, -1.0
    for item in candidates:
        if not item.embedding_vector:
            continue
        score = cosine_similarity(item.embedding_vector, payload.embedding_vector)
        if score > best_score:
            best_item, best_score = item, score

    if best_item is not None and best_score >= config.EMBEDDING_MATCH_THRESHOLD:
        best_item.status = "removed"
        best_item.removed_at = now
        best_item.last_seen_at = now
        db.commit()
        db.refresh(best_item)
        return schemas.TrajectoryResultOut(
            action="removed",
            item=schemas.FridgeItemOut.model_validate(best_item),
            matched_similarity=best_score,
        )

    # 매칭되는 항목을 찾지 못함 - 추적 불일치. 알림만 남기고 DB는 그대로 둔다.
    alert = Alert(
        alert_type="removal_unmatched",
        severity="info",
        message=(
            f"{payload.device_id}: '{payload.label}'({payload.class_name}) 반출 이벤트가 "
            f"감지됐지만 DB에서 일치하는 물건을 찾지 못했습니다"
            + (f" (최고 유사도 {best_score:.2f})" if best_score >= 0 else " (후보 없음)")
        ),
        device_id=payload.device_id,
    )
    db.add(alert)
    db.commit()
    return schemas.TrajectoryResultOut(
        action="unmatched_removal",
        item=None,
        matched_similarity=best_score if best_score >= 0 else None,
    )
