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

    class Config:
        from_attributes = True


class SettingsPatch(BaseModel):
    bg_theme: str

    @field_validator("bg_theme")
    @classmethod
    def valid_theme(cls, v: str) -> str:
        if v not in BG_THEMES:
            raise ValueError(f"bg_theme must be one of {BG_THEMES}")
        return v


class DeviceStateOut(BaseModel):
    bg_theme: str
    pending_count: int
    next_task: str | None = None


class LoginRequest(BaseModel):
    password: str
