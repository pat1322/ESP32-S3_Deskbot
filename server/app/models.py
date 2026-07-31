import uuid
from datetime import datetime

from sqlalchemy import Boolean, DateTime, Float, Integer, String
from sqlalchemy.orm import Mapped, mapped_column

from .db import Base

# Job lifecycle: queued -> downloading -> encoding -> ready -> playing -> done
#                                                    \-> error
JOB_ACTIVE_STATUSES = ("queued", "downloading", "encoding", "ready", "playing")

BG_THEMES = ("drift", "starfield", "minimal")
SETTINGS_ID = 1


class Job(Base):
    __tablename__ = "jobs"

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=lambda: str(uuid.uuid4()))
    video_id: Mapped[str] = mapped_column(String(32))
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

    # Website-driven WiFi switch: set by POST /api/settings/wifi, cleared by
    # the device's POST /device/wifi/ack once it's tried the new network.
    pending_wifi_ssid: Mapped[str | None] = mapped_column(String(64), nullable=True)
    pending_wifi_password: Mapped[str | None] = mapped_column(String(128), nullable=True)
    wifi_apply_status: Mapped[str] = mapped_column(String(16), default="none")  # none|applying|applied|failed

    updated_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)


class Todo(Base):
    __tablename__ = "todos"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    text: Mapped[str] = mapped_column(String(256))
    done: Mapped[bool] = mapped_column(Boolean, default=False)

    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow)
    updated_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow, onupdate=datetime.utcnow)


class Quote(Base):
    """Short messages rotated on the idle clock screen (idle_screen.cpp,
    the blank area between the date and the to-do line)."""

    __tablename__ = "quotes"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    text: Mapped[str] = mapped_column(String(64))

    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow)
