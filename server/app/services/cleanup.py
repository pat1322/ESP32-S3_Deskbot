import asyncio
import logging
import shutil
from datetime import datetime, timedelta

from ..config import settings
from ..db import SessionLocal
from ..models import Job
from .job_worker import job_dir

logger = logging.getLogger("deskbot.cleanup")

_cleanup_task: asyncio.Task | None = None


def _sweep_once() -> None:
    db = SessionLocal()
    try:
        now = datetime.utcnow()
        ready_cutoff = now - timedelta(seconds=settings.ready_grace_period_s)
        error_cutoff = now - timedelta(seconds=settings.error_grace_period_s)

        stale_ready = (
            db.query(Job)
            .filter(Job.status.in_(["ready", "playing"]))
            .filter(Job.ready_at.isnot(None))
            .filter(Job.ready_at < ready_cutoff)
            .all()
        )
        for job in stale_ready:
            shutil.rmtree(job_dir(job.id), ignore_errors=True)
            job.status = "done"
            job.mjpeg_path = None
            job.mp3_path = None
            logger.info("swept stale job %s (ready_at=%s)", job.id, job.ready_at)

        stale_errors = (
            db.query(Job)
            .filter(Job.status == "error")
            .filter(Job.updated_at < error_cutoff)
            .all()
        )
        for job in stale_errors:
            shutil.rmtree(job_dir(job.id), ignore_errors=True)
            job.status = "done"

        if stale_ready or stale_errors:
            db.commit()
    finally:
        db.close()


async def _loop() -> None:
    while True:
        await asyncio.sleep(settings.cleanup_interval_s)
        try:
            _sweep_once()
        except Exception:  # noqa: BLE001 - never let cleanup crash the app
            logger.exception("cleanup sweep failed")


def start() -> None:
    global _cleanup_task
    if _cleanup_task is None:
        _cleanup_task = asyncio.create_task(_loop())


async def stop() -> None:
    global _cleanup_task
    if _cleanup_task is not None:
        _cleanup_task.cancel()
        _cleanup_task = None
