from datetime import datetime, timedelta

from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from ..auth import require_web_session
from ..db import get_db
from ..models import SETTINGS_ID, Settings
from ..schemas import FocusStartIn, SettingsOut, SettingsPatch, WifiSubmitIn

router = APIRouter(tags=["settings"], dependencies=[Depends(require_web_session)])


def get_or_create_settings(db: Session) -> Settings:
    s = db.get(Settings, SETTINGS_ID)
    if s is None:
        s = Settings(id=SETTINGS_ID)
        db.add(s)
        db.commit()
        db.refresh(s)

    # db.sync_schema()'s ALTER TABLE pass always adds new columns nullable
    # at the DB level (existing rows have nothing to backfill) — see
    # CLAUDE.md — even though focus_active/focus_label are declared
    # non-nullable in the model and response schemas. On a row that
    # existed before those columns were added, they'd read back as None
    # and fail response validation, so coalesce once here.
    dirty = False
    if s.focus_active is None:
        s.focus_active = False
        dirty = True
    if s.focus_label is None:
        s.focus_label = "Focus"
        dirty = True
    if dirty:
        db.commit()
        db.refresh(s)

    # Lazily clear an expired focus session on read, rather than a
    # separate timer — the countdown is already fully computed from
    # focus_end_at, so this just stops it reporting "active" forever once
    # it hits 0.
    if s.focus_active and s.focus_seconds_remaining <= 0:
        s.focus_active = False
        s.focus_end_at = None
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


@router.post("/api/focus/start", response_model=SettingsOut)
def start_focus(body: FocusStartIn, db: Session = Depends(get_db)):
    s = get_or_create_settings(db)
    s.focus_active = True
    s.focus_end_at = datetime.utcnow() + timedelta(minutes=body.minutes)
    s.focus_label = body.label.strip()[:32] or "Focus"
    db.commit()
    db.refresh(s)
    return s


@router.post("/api/focus/stop", response_model=SettingsOut)
def stop_focus(db: Session = Depends(get_db)):
    s = get_or_create_settings(db)
    s.focus_active = False
    s.focus_end_at = None
    db.commit()
    db.refresh(s)
    return s
