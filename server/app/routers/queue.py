import shutil

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ..auth import require_web_session
from ..config import settings
from ..db import get_db
from ..models import JOB_ACTIVE_STATUSES, MAX_QUEUE_DEPTH, Job
from ..schemas import JobOut, QueueRequest
from ..services import job_worker

router = APIRouter(tags=["queue"], dependencies=[Depends(require_web_session)])


def _to_out(job: Job) -> JobOut:
    return JobOut(
        job_id=job.id,
        status=job.status,
        title=job.title,
        error_message=job.error_message,
        # source_type/paused are nullable at the DB level (added to an
        # already-existing table — see CLAUDE.md) even though they're
        # non-Optional here, so coalesce an old NULL row to its intended
        # default rather than failing response validation.
        source_type=job.source_type or "youtube",
        paused=bool(job.paused),
        created_at=job.created_at,
    )


@router.post("/api/queue", response_model=JobOut, status_code=201)
async def queue_video(body: QueueRequest, db: Session = Depends(get_db)):
    active_count = db.query(Job).filter(Job.status.in_(JOB_ACTIVE_STATUSES)).count()
    if active_count >= MAX_QUEUE_DEPTH:
        raise HTTPException(status_code=409, detail=f"queue is full (max {MAX_QUEUE_DEPTH})")

    job = Job(video_id=body.video_id.strip(), fps=settings.default_fps, fps_low=settings.low_fps)
    db.add(job)
    db.commit()
    db.refresh(job)

    await job_worker.enqueue(job.id)
    return _to_out(job)


@router.get("/api/jobs", response_model=list[JobOut])
def list_jobs(active: bool = False, db: Session = Depends(get_db)):
    q = db.query(Job)
    if active:
        q = q.filter(Job.status.in_(JOB_ACTIVE_STATUSES))
    jobs = q.order_by(Job.created_at.desc()).limit(20).all()
    return [_to_out(j) for j in jobs]


@router.get("/api/jobs/{job_id}", response_model=JobOut)
def get_job(job_id: str, db: Session = Depends(get_db)):
    job = db.get(Job, job_id)
    if job is None:
        raise HTTPException(status_code=404, detail="job not found")
    return _to_out(job)


@router.post("/api/jobs/{job_id}/cancel", response_model=JobOut)
async def cancel_job(job_id: str, db: Session = Depends(get_db)):
    job = db.get(Job, job_id)
    if job is None:
        raise HTTPException(status_code=404, detail="job not found")
    # "error" isn't in JOB_ACTIVE_STATUSES (it's terminal, like "cancelled"/
    # "done"), but it's still reachable from the website's "Now playing"
    # panel via a Dismiss button -- without this, a permanently-broken
    # upload (bad file, corrupt encode) had no way to be cleared except
    # waiting for cleanup.py's up-to-15-minute sweep, since Retry alone
    # just reruns the same failing pipeline against the same bad source.
    if job.status not in JOB_ACTIVE_STATUSES and job.status != "error":
        raise HTTPException(status_code=409, detail="job is not active")

    if job.status in ("downloading", "encoding"):
        # process_job's CancelledError handler does the DB update + cleanup
        job_worker.cancel_job(job_id)
    else:
        # queued (not yet started) or ready/playing (no subprocess running,
        # unconsumed media just sitting on disk) — clean up directly. A
        # "playing" job is picked up by the device's mid-playback cancel
        # check (video_player.cpp polls /video/status every few seconds)
        # within a few seconds of this status flipping away from "playing".
        shutil.rmtree(job_worker.job_dir(job_id), ignore_errors=True)
        job.status = "cancelled"
        job.error_message = "Cancelled by user"
        job.mjpeg_path = None
        job.mjpeg_path_low = None
        job.mp3_path = None
        db.commit()
        db.refresh(job)
    return _to_out(job)


@router.post("/api/jobs/{job_id}/pause", response_model=JobOut)
def pause_job(job_id: str, db: Session = Depends(get_db)):
    job = db.get(Job, job_id)
    if job is None:
        raise HTTPException(status_code=404, detail="job not found")
    if job.status != "playing":
        raise HTTPException(status_code=409, detail="job is not playing")
    job.paused = True
    db.commit()
    db.refresh(job)
    return _to_out(job)


@router.post("/api/jobs/{job_id}/resume", response_model=JobOut)
def resume_job(job_id: str, db: Session = Depends(get_db)):
    job = db.get(Job, job_id)
    if job is None:
        raise HTTPException(status_code=404, detail="job not found")
    job.paused = False
    db.commit()
    db.refresh(job)
    return _to_out(job)


@router.post("/api/jobs/{job_id}/retry", response_model=JobOut)
async def retry_job(job_id: str, db: Session = Depends(get_db)):
    job = db.get(Job, job_id)
    if job is None:
        raise HTTPException(status_code=404, detail="job not found")
    if job.status != "error":
        raise HTTPException(status_code=409, detail="only errored jobs can be retried")

    active_count = db.query(Job).filter(Job.status.in_(JOB_ACTIVE_STATUSES)).count()
    if active_count >= MAX_QUEUE_DEPTH:
        raise HTTPException(status_code=409, detail=f"queue is full (max {MAX_QUEUE_DEPTH})")

    job.status = "queued"
    job.error_message = None
    job.mjpeg_path = None
    job.mjpeg_path_low = None
    job.mp3_path = None
    db.commit()
    db.refresh(job)

    await job_worker.enqueue(job.id)
    return _to_out(job)
