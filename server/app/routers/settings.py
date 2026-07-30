from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session

from ..auth import require_web_session
from ..db import get_db
from ..models import SETTINGS_ID, Settings
from ..schemas import SettingsOut, SettingsPatch

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
    s.bg_theme = body.bg_theme
    db.commit()
    db.refresh(s)
    return s
