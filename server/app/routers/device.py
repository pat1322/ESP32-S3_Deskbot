from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from ..auth import require_device_or_web
from ..db import get_db
from ..models import Todo
from ..schemas import DeviceStateOut, WifiAckIn
from .settings import get_or_create_settings

router = APIRouter(tags=["device"], dependencies=[Depends(require_device_or_web)])


@router.get("/device/state", response_model=DeviceStateOut)
def device_state(db: Session = Depends(get_db)):
    settings = get_or_create_settings(db)
    pending = db.query(Todo).filter(Todo.done.is_(False)).order_by(Todo.created_at.asc()).all()

    applying = settings.wifi_apply_status == "applying"
    return DeviceStateOut(
        bg_theme=settings.bg_theme,
        volume=settings.volume,
        pending_count=len(pending),
        next_task=pending[0].text if pending else None,
        pending_wifi_ssid=settings.pending_wifi_ssid if applying else None,
        pending_wifi_password=settings.pending_wifi_password if applying else None,
    )


@router.post("/device/wifi/ack")
def wifi_ack(body: WifiAckIn, db: Session = Depends(get_db)):
    s = get_or_create_settings(db)
    s.wifi_apply_status = body.status
    s.pending_wifi_ssid = None
    s.pending_wifi_password = None
    db.commit()
    return {"ok": True}
