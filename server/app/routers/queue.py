import shutil

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ..auth import require_web_session
from ..config import settings
from ..db import get_db
from ..models import JOB_ACTIVE_STATUSES, Job
from ..schemas import JobOut, QueueRequest
from ..services import job_worker

router = APIRouter(tags=["queue"], dependencies=[Depends(require_web_session)])


def _to_out(job: Job) -> JobOut:
    return JobOut(
        job_id=job.id,
        status=job.status,
        title=job.title,
        error_message=job.error_message,
        created_at=job.created_at,
    )


@router.post("/api/queue", response_model=JobOut, status_code=201)
async def queue_video(body: QueueRequest, db: Session = Depends(get_db)):
    existing = db.query(Job).filter(Job.status.in_(JOB_ACTIVE_STATUSES)).first()
    if existing is not None:
        raise HTTPException(status_code=409, detail="a job is already in progress")

    job = Job(video_id=body.video_id.strip(), fps=settings.default_fps)
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
    if job.status not in JOB_ACTIVE_STATUSES:
        raise HTTPException(status_code=409, detail="job is not active")

    if job.status in ("downloading", "encoding"):
        # process_job's CancelledError handler does the DB update + cleanup
        job_worker.cancel_job(job_id)
    else:
        # queued (not yet started) or ready/playing (no subprocess running,
        # unconsumed media just sitting on disk) — clean up directly.
        shutil.rmtree(job_worker.job_dir(job_id), ignore_errors=True)
        job.status = "error"
        job.error_message = "Cancelled by user"
        job.mjpeg_path = None
        job.mp3_path = None
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

    other_active = db.query(Job).filter(Job.status.in_(JOB_ACTIVE_STATUSES), Job.id != job.id).first()
    if other_active is not None:
        raise HTTPException(status_code=409, detail="another job is already in progress")

    job.status = "queued"
    job.error_message = None
    job.mjpeg_path = None
    job.mp3_path = None
    db.commit()
    db.refresh(job)

    await job_worker.enqueue(job.id)
    return _to_out(job)
