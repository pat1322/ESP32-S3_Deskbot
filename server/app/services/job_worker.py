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
_running_tasks: dict[str, asyncio.Task] = {}
_shutting_down = False


def job_dir(job_id: str) -> str:
    return os.path.join(settings.media_dir, job_id)


async def enqueue(job_id: str) -> None:
    await _queue.put(job_id)


def cancel_job(job_id: str) -> bool:
    """Best-effort cancel of a job that's actively downloading/encoding.

    Killing the yt-dlp download's worker thread isn't possible (Python
    threads can't be force-stopped), so a cancel during the download phase
    leaves that thread to finish in the background — its result is simply
    discarded when process_job unwinds. An in-flight ffmpeg subprocess *is*
    killed directly (see ffmpeg_service._run).
    """
    task = _running_tasks.get(job_id)
    if task is None:
        return False
    task.cancel()
    return True


async def process_job(job_id: str) -> None:
    db = SessionLocal()
    try:
        job = db.get(Job, job_id)
        if job is None or job.status != "queued":
            return  # stale/already-cancelled job popped off the queue

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
            mjpeg_path = os.path.join(out_dir, "video_high.mjpeg")
            mjpeg_path_low = os.path.join(out_dir, "video_low.mjpeg")
            await asyncio.gather(
                ffmpeg_service.extract_audio(result.source_path, mp3_path),
                ffmpeg_service.extract_mjpeg(
                    result.source_path, mjpeg_path, fps=job.fps, quality=settings.jpeg_q
                ),
                ffmpeg_service.extract_mjpeg(
                    result.source_path, mjpeg_path_low, fps=job.fps_low, quality=settings.low_jpeg_q
                ),
            )

            if os.path.exists(result.source_path):
                os.remove(result.source_path)

            job.mp3_path = mp3_path
            job.mjpeg_path = mjpeg_path
            job.mjpeg_path_low = mjpeg_path_low
            job.status = "ready"
            job.ready_at = datetime.utcnow()
            db.commit()
            logger.info("job %s ready (%s)", job_id, job.title)

        except asyncio.CancelledError:
            logger.info("job %s cancelled by user", job_id)
            job.status = "cancelled"
            job.error_message = "Cancelled by user"
            db.commit()
            shutil.rmtree(out_dir, ignore_errors=True)
            raise

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
        task = asyncio.create_task(process_job(job_id))
        _running_tasks[job_id] = task
        try:
            await task
        except asyncio.CancelledError:
            if _shutting_down:
                raise  # finally below still runs task_done() exactly once
            # a single job was cancelled (task.cancel() via cancel_job) —
            # process_job already handled cleanup, keep the loop running.
        except Exception:
            logger.exception("job %s failed unexpectedly", job_id)
        finally:
            _running_tasks.pop(job_id, None)
            _queue.task_done()


def start() -> None:
    global _worker_task
    if _worker_task is None:
        _worker_task = asyncio.create_task(_worker_loop())


async def stop() -> None:
    global _worker_task, _shutting_down
    _shutting_down = True
    if _worker_task is not None:
        _worker_task.cancel()
        try:
            await _worker_task
        except asyncio.CancelledError:
            pass
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
