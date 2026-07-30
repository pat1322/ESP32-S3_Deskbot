from datetime import datetime

from pydantic import BaseModel, field_validator

from .models import BG_THEMES


class SearchResult(BaseModel):
    video_id: str
    title: str
    duration: int | None = None
    thumbnail_url: str | None = None
    channel: str | None = None


class SearchResponse(BaseModel):
    results: list[SearchResult]


class QueueRequest(BaseModel):
    video_id: str


class JobOut(BaseModel):
    job_id: str
    status: str
    title: str | None = None
    error_message: str | None = None
    created_at: datetime

    class Config:
        from_attributes = True


class CurrentJobOut(BaseModel):
    ready: bool
    job_id: str | None = None


class TodoIn(BaseModel):
    text: str


class TodoPatch(BaseModel):
    done: bool


class TodoOut(BaseModel):
    id: int
    text: str
    done: bool

    class Config:
        from_attributes = True


class TodoSummaryOut(BaseModel):
    pending_count: int
    next_task: str | None = None


class SettingsOut(BaseModel):
    bg_theme: str
    volume: float
    wifi_apply_status: str
    pending_wifi_ssid: str | None = None  # never echoes the password back

    class Config:
        from_attributes = True


class SettingsPatch(BaseModel):
    bg_theme: str | None = None
    volume: float | None = None

    @field_validator("bg_theme")
    @classmethod
    def valid_theme(cls, v: str | None) -> str | None:
        if v is not None and v not in BG_THEMES:
            raise ValueError(f"bg_theme must be one of {BG_THEMES}")
        return v

    @field_validator("volume")
    @classmethod
    def valid_volume(cls, v: float | None) -> float | None:
        if v is not None and not (0.0 <= v <= 1.0):
            raise ValueError("volume must be between 0.0 and 1.0")
        return v


class WifiSubmitIn(BaseModel):
    ssid: str
    password: str


class WifiAckIn(BaseModel):
    status: str  # "applied" | "failed"

    @field_validator("status")
    @classmethod
    def valid_status(cls, v: str) -> str:
        if v not in ("applied", "failed"):
            raise ValueError('status must be "applied" or "failed"')
        return v


class DeviceStateOut(BaseModel):
    bg_theme: str
    volume: float
    pending_count: int
    next_task: str | None = None
    pending_wifi_ssid: str | None = None
    pending_wifi_password: str | None = None


class LoginRequest(BaseModel):
    password: str
