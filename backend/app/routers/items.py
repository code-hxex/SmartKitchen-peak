"""현재 상태 조회 API (프론트엔드/대시보드에서 사용)."""

import os
from typing import Optional

from fastapi import APIRouter, Depends, Query
from sqlalchemy import select
from sqlalchemy.orm import Session

from .. import config, schemas
from ..database import get_db
from ..models import FridgeItem

router = APIRouter(prefix="/api/items", tags=["items"])


@router.get("", response_model=list[schemas.FridgeItemOut])
def list_items(status: Optional[str] = Query(default="in_fridge"), db: Session = Depends(get_db)):
    stmt = select(FridgeItem)
    if status:
        stmt = stmt.where(FridgeItem.status == status)
    items = db.execute(stmt.order_by(FridgeItem.entry_date.desc())).scalars().all()

    results = []
    for item in items:
        out = schemas.FridgeItemOut.model_validate(item)
        frame_path = os.path.join(config.REGISTRATION_FRAME_DIR, f"{item.item_id}.jpg")
        if os.path.exists(frame_path):
            out.image_url = f"/static/registration_frames/{item.item_id}.jpg"
        results.append(out)
    return results
