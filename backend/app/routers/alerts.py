"""알림 조회 API (가스 이상치, 재점검 필요, 반출 불일치 등)."""

from fastapi import APIRouter, Depends
from sqlalchemy import select
from sqlalchemy.orm import Session

from .. import schemas
from ..database import get_db
from ..models import Alert

router = APIRouter(prefix="/api/alerts", tags=["alerts"])


@router.get("", response_model=list[schemas.AlertOut])
def list_alerts(db: Session = Depends(get_db)):
    return db.execute(select(Alert).order_by(Alert.created_at.desc()).limit(100)).scalars().all()
