import time

from fastapi import APIRouter, Depends, Request
from sqlalchemy.orm import Session

from ..auth import require_device_or_web
from ..db import get_db
from ..models import Quote, Todo
from ..schemas import DeviceLogOut, DeviceStateOut, WifiAckIn
from ..services import device_log
from .settings import get_or_create_settings

router = APIRouter(tags=["device"], dependencies=[Depends(require_device_or_web)])

# How long each quote stays on screen before rotating to the next, in
# seconds. Index-by-time rather than persisting a "current index" — cheap
# and self-synchronizing across every poller (device + any open web tabs).
QUOTE_ROTATE_S = 30


def _pick_quote(db: Session) -> str | None:
    quotes = db.query(Quote).order_by(Quote.id.asc()).all()
    if not quotes:
        return None
    return quotes[int(time.time() / QUOTE_ROTATE_S) % len(quotes)].text


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
        quote=_pick_quote(db),
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


@router.post("/device/log")
async def post_device_log(request: Request):
    # Raw text body (one log line per line), not JSON — avoids needing the
    # firmware to JSON-escape arbitrary Serial-style output (SSIDs, error
    # strings, etc.) for what's purely a debugging channel.
    body = await request.body()
    text = body.decode("utf-8", errors="replace")
    lines = text.splitlines()[:50]  # guard against a runaway payload
    device_log.append(lines)
    return {"ok": True}


@router.get("/api/device/log", response_model=DeviceLogOut)
def get_device_log():
    return DeviceLogOut(entries=device_log.recent())


@router.post("/api/device/log/clear")
def clear_device_log():
    device_log.clear()
    return {"ok": True}
