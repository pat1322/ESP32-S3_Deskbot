from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from ..auth import require_web_session
from ..db import get_db
from ..models import SETTINGS_ID, Settings
from ..schemas import SettingsOut, SettingsPatch, WifiSubmitIn

router = APIRouter(tags=["settings"], dependencies=[Depends(require_web_session)])


def get_or_create_settings(db: Session) -> Settings:
    s = db.get(Settings, SETTINGS_ID)
    if s is None:
        s = Settings(id=SETTINGS_ID)
        db.add(s)
        db.commit()
        db.refresh(s)
    return s


@router.get("/api/settings", response_model=SettingsOut)
def get_settings(db: Session = Depends(get_db)):
    return get_or_create_settings(db)


@router.patch("/api/settings", response_model=SettingsOut)
def update_settings(body: SettingsPatch, db: Session = Depends(get_db)):
    s = get_or_create_settings(db)
    if body.bg_theme is not None:
        s.bg_theme = body.bg_theme
    if body.volume is not None:
        s.volume = body.volume
    db.commit()
    db.refresh(s)
    return s


@router.post("/api/settings/wifi", response_model=SettingsOut)
def submit_wifi(body: WifiSubmitIn, db: Session = Depends(get_db)):
    s = get_or_create_settings(db)
    s.pending_wifi_ssid = body.ssid.strip()
    s.pending_wifi_password = body.password
    s.wifi_apply_status = "applying"
    db.commit()
    db.refresh(s)
    return s


@router.post("/api/settings/wifi/dismiss", response_model=SettingsOut)
def dismiss_wifi_status(db: Session = Depends(get_db)):
    s = get_or_create_settings(db)
    s.wifi_apply_status = "none"
    db.commit()
    db.refresh(s)
    return s
