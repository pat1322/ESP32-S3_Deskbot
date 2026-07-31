import os
import shutil
import uuid

import aiofiles
from fastapi import APIRouter, Depends, HTTPException, UploadFile
from sqlalchemy.orm import Session

from ..auth import require_web_session
from ..config import settings
from ..db import get_db
from ..models import JOB_ACTIVE_STATUSES, MAX_QUEUE_DEPTH, Job, Photo
from ..schemas import JobOut, SettingsOut
from ..services import ffmpeg_service, job_worker
from .settings import get_or_create_settings

router = APIRouter(tags=["upload"], dependencies=[Depends(require_web_session)])

_CHUNK_SIZE = 1024 * 1024


async def _save_upload(file: UploadFile, dest_path: str, max_mb: int) -> None:
    """Streams file to dest_path in chunks, enforcing max_mb by counting
    actual bytes written rather than trusting the Content-Length header
    (which the client controls and could be wrong or absent)."""
    max_bytes = max_mb * 1024 * 1024
    written = 0
    async with aiofiles.open(dest_path, "wb") as out:
        while chunk := await file.read(_CHUNK_SIZE):
            written += len(chunk)
            if written > max_bytes:
                raise HTTPException(status_code=413, detail=f"file exceeds {max_mb}MB limit")
            await out.write(chunk)
    if written == 0:
        raise HTTPException(status_code=400, detail="empty file")


@router.post("/api/upload/video", response_model=JobOut, status_code=201)
async def upload_video(file: UploadFile, db: Session = Depends(get_db)):
    active_count = db.query(Job).filter(Job.status.in_(JOB_ACTIVE_STATUSES)).count()
    if active_count >= MAX_QUEUE_DEPTH:
        raise HTTPException(status_code=409, detail=f"queue is full (max {MAX_QUEUE_DEPTH})")

    job = Job(
        source_type="upload",
        title=file.filename or "Uploaded video",
        fps=settings.default_fps,
        fps_low=settings.low_fps,
    )
    db.add(job)
    db.commit()
    db.refresh(job)

    out_dir = job_worker.job_dir(job.id)
    os.makedirs(out_dir, exist_ok=True)
    ext = os.path.splitext(file.filename or "")[1] or ".mp4"
    dest_path = os.path.join(out_dir, f"source{ext}")

    try:
        await _save_upload(file, dest_path, settings.max_upload_video_mb)
    except HTTPException:
        db.delete(job)
        db.commit()
        shutil.rmtree(out_dir, ignore_errors=True)
        raise

    job.source_path = dest_path
    db.commit()
    db.refresh(job)

    await job_worker.enqueue(job.id)
    return JobOut(
        job_id=job.id,
        status=job.status,
        title=job.title,
        error_message=job.error_message,
        source_type=job.source_type,
        created_at=job.created_at,
    )


@router.post("/api/upload/photo", response_model=SettingsOut)
async def upload_photo(file: UploadFile, db: Session = Depends(get_db)):
    photo_dir = os.path.join(settings.media_dir, "photos")
    os.makedirs(photo_dir, exist_ok=True)

    photo_id = str(uuid.uuid4())
    ext = os.path.splitext(file.filename or "")[1] or ".jpg"
    raw_path = os.path.join(photo_dir, f"{photo_id}_raw{ext}")
    final_path = os.path.join(photo_dir, f"{photo_id}.jpg")

    await _save_upload(file, raw_path, settings.max_upload_photo_mb)

    try:
        # ffmpeg treats a still image input as a one-frame "video", so the
        # same crop-to-fill resize extract_mjpeg uses for video frames
        # works unchanged here — no separate image-processing dependency.
        await ffmpeg_service.extract_still(raw_path, final_path)
    except ffmpeg_service.FfmpegError as exc:
        raise HTTPException(status_code=400, detail=f"could not process image: {exc}") from exc
    finally:
        if os.path.exists(raw_path):
            os.remove(raw_path)

    s = get_or_create_settings(db)
    if s.current_photo_id:
        old = db.get(Photo, s.current_photo_id)
        if old is not None:
            if os.path.exists(old.path):
                os.remove(old.path)
            db.delete(old)

    db.add(Photo(id=photo_id, path=final_path))
    s.current_photo_id = photo_id
    db.commit()
    db.refresh(s)
    return s


@router.post("/api/photos/current/clear", response_model=SettingsOut)
def clear_photo(db: Session = Depends(get_db)):
    s = get_or_create_settings(db)
    if s.current_photo_id:
        old = db.get(Photo, s.current_photo_id)
        if old is not None:
            if os.path.exists(old.path):
                os.remove(old.path)
            db.delete(old)
        s.current_photo_id = None
        db.commit()
        db.refresh(s)
    return s
