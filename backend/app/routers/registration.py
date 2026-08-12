"""초기 등록 API - 스마트폰으로 촬영한 이미지를 처리해서 물건을 등록한다."""

import base64
import os
from datetime import datetime, timezone

from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from .. import config, schemas
from ..database import get_db
from ..embeddings import compute_embedding
from ..models import FridgeItem
from ..vision import identify_for_registration

router = APIRouter(prefix="/api/register", tags=["registration"])


@router.post("", response_model=schemas.RegistrationOut)
def register_item(payload: schemas.RegistrationIn, db: Session = Depends(get_db)):
    jpeg_bytes = base64.b64decode(payload.image_base64)

    result = identify_for_registration(jpeg_bytes)
    embedding = compute_embedding(jpeg_bytes)

    now = datetime.now(timezone.utc)
    item = FridgeItem(
        class_name=result["class_name"],
        label=result["label"],
        entry_date=now,
        status="in_fridge",
        embedding_vector=embedding,
        confidence=result["confidence"],
        last_seen_at=now,
        source="registration",
    )
    db.add(item)
    db.commit()
    db.refresh(item)

    os.makedirs(config.REGISTRATION_FRAME_DIR, exist_ok=True)
    with open(os.path.join(config.REGISTRATION_FRAME_DIR, f"{item.item_id}.jpg"), "wb") as f:
        f.write(jpeg_bytes)

    return schemas.RegistrationOut(
        item=schemas.FridgeItemOut.model_validate(item),
        raw_recognition=result,
    )


@router.post("/confirm", response_model=schemas.FridgeItemOut)
def register_confirmed_item(payload: schemas.ConfirmedItemIn, db: Session = Depends(get_db)):
    """다른 곳에서 이미 인식을 마친 물건을 그대로 등록한다 (Claude 재호출 없음)."""
    jpeg_bytes = base64.b64decode(payload.image_base64)
    embedding = compute_embedding(jpeg_bytes)

    now = datetime.now(timezone.utc)
    item = FridgeItem(
        class_name=payload.class_name,
        label=payload.label,
        entry_date=now,
        status="in_fridge",
        embedding_vector=embedding,
        confidence=payload.confidence,
        last_seen_at=now,
        source="live_detection",
    )
    db.add(item)
    db.commit()
    db.refresh(item)

    os.makedirs(config.REGISTRATION_FRAME_DIR, exist_ok=True)
    with open(os.path.join(config.REGISTRATION_FRAME_DIR, f"{item.item_id}.jpg"), "wb") as f:
        f.write(jpeg_bytes)

    return schemas.FridgeItemOut.model_validate(item)
