import uuid
from datetime import datetime

from sqlalchemy import Boolean, DateTime, Float, Integer, String
from sqlalchemy.orm import Mapped, mapped_column

from .db import Base

# Job lifecycle: queued -> downloading -> encoding -> ready -> playing -> done
#                                                    \-> error
JOB_ACTIVE_STATUSES = ("queued", "downloading", "encoding", "ready", "playing")

# Multiple videos (from search or upload) can be queued at once —
# services/job_worker.py's worker loop already processes them strictly
# FIFO, one at a time — this is just a sanity cap so the queue can't grow
# unbounded. Shared by routers/queue.py and routers/upload.py.
MAX_QUEUE_DEPTH = 10

BG_THEMES = ("drift", "starfield", "minimal")
ORIENTATIONS = ("landscape", "portrait")
SETTINGS_ID = 1


class Job(Base):
    __tablename__ = "jobs"

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    # Null for source_type="upload" (there's no YouTube ID) — only ever
    # required for "youtube" jobs.
    video_id: Mapped[str | None] = mapped_column(String(32), nullable=True)
    source_type: Mapped[str] = mapped_column(String(16), default="youtube")  # youtube|upload
    title: Mapped[str | None] = mapped_column(String(256), nullable=True)
    status: Mapped[str] = mapped_column(String(16), default="queued")
    error_message: Mapped[str | None] = mapped_column(String(512), nullable=True)

    source_path: Mapped[str | None] = mapped_column(String(512), nullable=True)
    mjpeg_path: Mapped[str | None] = mapped_column(String(512), nullable=True)
    mjpeg_path_low: Mapped[str | None] = mapped_column(String(512), nullable=True)
    mp3_path: Mapped[str | None] = mapped_column(String(512), nullable=True)
    fps: Mapped[int] = mapped_column(Integer, default=15)
    fps_low: Mapped[int] = mapped_column(Integer, default=8)

    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow)
    updated_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)
    ready_at: Mapped[datetime | None] = mapped_column(DateTime, nullable=True)
    consumed_at: Mapped[datetime | None] = mapped_column(DateTime, nullable=True)


class Settings(Base):
    """Single-row table (id is always SETTINGS_ID) holding device-wide prefs."""

    __tablename__ = "settings"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    bg_theme: Mapped[str] = mapped_column(String(16), default="drift")
    volume: Mapped[float] = mapped_column(Float, default=0.4)
    orientation: Mapped[str] = mapped_column(String(16), default="landscape")  # landscape|portrait

    # Website-driven WiFi switch: set by POST /api/settings/wifi, cleared by
    # the device's POST /device/wifi/ack once it's tried the new network.
    pending_wifi_ssid: Mapped[str | None] = mapped_column(String(64), nullable=True)
    pending_wifi_password: Mapped[str | None] = mapped_column(String(128), nullable=True)
    wifi_apply_status: Mapped[str] = mapped_column(String(16), default="none")  # none|applying|applied|failed

    # Focus/Pomodoro timer: set by POST /api/focus/start, cleared by
    # POST /api/focus/stop. focus_end_at is the source of truth for the
    # countdown — remaining time is always computed as (focus_end_at -
    # now), never decremented/stored, so it stays correct across restarts
    # and multiple pollers without any clock-sync bookkeeping.
    focus_active: Mapped[bool] = mapped_column(Boolean, default=False)
    focus_end_at: Mapped[datetime | None] = mapped_column(DateTime, nullable=True)
    focus_label: Mapped[str] = mapped_column(String(32), default="Focus")

    # Photo display: the one currently-shown Photo, if any. Nullable with
    # no default needed (unlike focus_active/orientation above) — None is
    # already the correct "nothing showing" value, so this column doesn't
    # need the read-time coalesce those two do on an upgraded row.
    current_photo_id: Mapped[str | None] = mapped_column(String(36), nullable=True)

    updated_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)

    @property
    def focus_seconds_remaining(self) -> int:
        # Computed on every read from focus_end_at rather than decremented
        # anywhere, so it's always correct regardless of how many pollers
        # (device + web tabs) ask, and survives a server restart untouched.
        if not self.focus_active or self.focus_end_at is None:
            return 0
        remaining = (self.focus_end_at - datetime.utcnow()).total_seconds()
        return max(0, int(remaining))

    @property
    def photo_active(self) -> bool:
        return self.current_photo_id is not None

    @property
    def photo_id(self) -> str | None:
        return self.current_photo_id


class Todo(Base):
    __tablename__ = "todos"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    text: Mapped[str] = mapped_column(String(256))
    done: Mapped[bool] = mapped_column(Boolean, default=False)

    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow)
    updated_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)


class Photo(Base):
    """An uploaded still image, resized/cropped to device resolution.
    Settings.current_photo_id points at whichever one (if any) is the
    device's currently-shown photo — there's no queue/history semantics
    here, just a single active pointer, same shape as the focus timer."""

    __tablename__ = "photos"

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    path: Mapped[str] = mapped_column(String(512))

    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow)


class Quote(Base):
    """Short messages rotated on the idle clock screen (idle_screen.cpp,
    the blank area between the date and the to-do line)."""

    __tablename__ = "quotes"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    text: Mapped[str] = mapped_column(String(64))

    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow)
