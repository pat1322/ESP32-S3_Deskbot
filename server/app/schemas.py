from datetime import datetime

from pydantic import BaseModel


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


class LoginRequest(BaseModel):
    password: str
