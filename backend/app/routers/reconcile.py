"""주기적 전체 재점검(reconcile) API.

궤적 분석은 개별 투입/반출 이벤트만 다루므로 놓치는 경우가 생길 수 있다.
이 엔드포인트는 외부(엣지/별도 풀스캔 파이프라인)에서 '지금 냉장고 안에 실제로
보이는 물건 전체 목록'을 보내면, DB의 in_fridge 상태와 비교해서 어긋난 부분을
바로잡는다:
  - DB에는 in_fridge인데 이번 스캔에 없으면 -> removed 처리 (모르는 새 빠져나간 것으로 간주)
  - 이번 스캔에는 있는데 DB에 없으면 -> 새로 등록 (모르는 새 들어온 것으로 간주)
  - 매칭되면 -> last_seen_at만 갱신

이와 별개로, main.py의 백그라운드 태스크가 일정 주기로 오래 안 보인 항목에
'재점검 필요' 알림을 남긴다 (풀스캔 입력 없이도 동작하는 최소한의 위생 점검).
"""

from datetime import datetime, timezone

from fastapi import APIRouter, Depends
from sqlalchemy import select
from sqlalchemy.orm import Session

from .. import config, schemas
from ..database import get_db
from ..embeddings import cosine_similarity
from ..models import FridgeItem

router = APIRouter(prefix="/api/reconcile", tags=["reconcile"])


@router.post("", response_model=schemas.ReconcileReportOut)
def reconcile(payload: schemas.ReconcileReportIn, db: Session = Depends(get_db)):
    now = datetime.now(timezone.utc)

    current_items = (
        db.execute(select(FridgeItem).where(FridgeItem.status == "in_fridge")).scalars().all()
    )
    unmatched_db_items = list(current_items)

    matched = 0
    newly_added = 0

    for scanned in payload.scanned_items:
        best_item, best_score, best_idx = None, -1.0, -1
        for idx, item in enumerate(unmatched_db_items):
            if item.class_name != scanned.class_name or not item.embedding_vector:
                continue
            score = cosine_similarity(item.embedding_vector, scanned.embedding_vector)
            if score > best_score:
                best_item, best_score, best_idx = item, score, idx

        if best_item is not None and best_score >= config.EMBEDDING_MATCH_THRESHOLD:
            best_item.last_seen_at = now
            matched += 1
            unmatched_db_items.pop(best_idx)
        else:
            db.add(
                FridgeItem(
                    class_name=scanned.class_name,
                    label=scanned.label,
                    entry_date=now,
                    status="in_fridge",
                    embedding_vector=scanned.embedding_vector,
                    confidence=scanned.confidence,
                    last_seen_at=now,
                    source="reconcile",
                )
            )
            newly_added += 1

    marked_removed = 0
    for leftover in unmatched_db_items:
        leftover.status = "removed"
        leftover.removed_at = now
        marked_removed += 1

    db.commit()

    total_in_fridge = len(
        db.execute(select(FridgeItem).where(FridgeItem.status == "in_fridge")).scalars().all()
    )

    return schemas.ReconcileReportOut(
        matched=matched,
        newly_added=newly_added,
        marked_removed=marked_removed,
        total_in_fridge=total_in_fridge,
    )
