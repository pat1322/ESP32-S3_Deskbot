import asyncio
import logging
import os
import shutil
from datetime import datetime

from ..config import settings
from ..db import SessionLocal
from ..models import Job
from . import ffmpeg_service, ytdlp_service

logger = logging.getLogger("deskbot.job_worker")

_queue: asyncio.Queue[str] = asyncio.Queue()
_worker_task: asyncio.Task | None = None


def job_dir(job_id: str) -> str:
    return os.path.join(settings.media_dir, job_id)


async def enqueue(job_id: str) -> None:
    await _queue.put(job_id)


async def process_job(job_id: str) -> None:
    db = SessionLocal()
    try:
        job = db.get(Job, job_id)
        if job is None:
            return

        out_dir = job_dir(job_id)
        os.makedirs(out_dir, exist_ok=True)

        try:
            meta = await ytdlp_service.get_metadata(job.video_id)
            duration = meta.get("duration")
            if not duration:
                raise ValueError("video has no fixed duration (likely a live stream) — not supported")
            if duration > settings.max_job_duration_s:
                raise ValueError(
                    f"video is {duration}s, exceeds {settings.max_job_duration_s}s limit"
                )

            job.status = "downloading"
            job.title = meta.get("title") or job.video_id
            db.commit()

            result = await ytdlp_service.download(job.video_id, out_dir)
            job.title = result.title
            job.status = "encoding"
            db.commit()

            mp3_path = os.path.join(out_dir, "audio.mp3")
            mjpeg_path = os.path.join(out_dir, "video.mjpeg")
            await asyncio.gather(
                ffmpeg_service.extract_audio(result.source_path, mp3_path),
                ffmpeg_service.extract_mjpeg(result.source_path, mjpeg_path, fps=job.fps),
            )

            if os.path.exists(result.source_path):
                os.remove(result.source_path)

            job.mp3_path = mp3_path
            job.mjpeg_path = mjpeg_path
            job.status = "ready"
            job.ready_at = datetime.utcnow()
            db.commit()
            logger.info("job %s ready (%s)", job_id, job.title)

        except Exception as exc:  # noqa: BLE001 - job errors must never crash the worker
            logger.exception("job %s failed", job_id)
            job.status = "error"
            job.error_message = str(exc)[:500]
            db.commit()
            shutil.rmtree(out_dir, ignore_errors=True)
    finally:
        db.close()


async def _worker_loop() -> None:
    while True:
        job_id = await _queue.get()
        try:
            await process_job(job_id)
        finally:
            _queue.task_done()


def start() -> None:
    global _worker_task
    if _worker_task is None:
        _worker_task = asyncio.create_task(_worker_loop())


async def stop() -> None:
    global _worker_task
    if _worker_task is not None:
        _worker_task.cancel()
        _worker_task = None


async def recover_and_requeue() -> None:
    """Run once at startup: resume queued jobs, fail out interrupted ones."""
    db = SessionLocal()
    try:
        queued = (
            db.query(Job)
            .filter(Job.status == "queued")
            .order_by(Job.created_at.asc())
            .all()
        )
        for job in queued:
            await enqueue(job.id)

        interrupted = db.query(Job).filter(Job.status.in_(["downloading", "encoding"])).all()
        for job in interrupted:
            job.status = "error"
            job.error_message = "interrupted by server restart"
        if interrupted:
            db.commit()
    finally:
        db.close()
