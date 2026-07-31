import os
from datetime import datetime

from fastapi import APIRouter, Depends, HTTPException
from fastapi.responses import StreamingResponse
from sqlalchemy.orm import Session

from ..auth import require_device_key
from ..db import get_db
from ..models import Job
from ..schemas import CurrentJobOut, JobOut

router = APIRouter(tags=["video"], dependencies=[Depends(require_device_key)])

CHUNK_SIZE = 64 * 1024


def _file_chunks(path: str):
    with open(path, "rb") as f:
        while True:
            chunk = f.read(CHUNK_SIZE)
            if not chunk:
                break
            yield chunk


@router.get("/video/current", response_model=CurrentJobOut)
def current(db: Session = Depends(get_db)):
    job = db.query(Job).filter(Job.status == "ready").order_by(Job.ready_at.asc()).first()
    if job is None:
        return CurrentJobOut(ready=False)
    return CurrentJobOut(ready=True, job_id=job.id)


@router.get("/video/status/{job_id}", response_model=JobOut)
def status(job_id: str, db: Session = Depends(get_db)):
    job = db.get(Job, job_id)
    if job is None:
        raise HTTPException(status_code=404, detail="job not found")
    return JobOut(
        job_id=job.id,
        status=job.status,
        title=job.title,
        error_message=job.error_message,
        created_at=job.created_at,
    )


@router.post("/video/current/clear")
def clear_current(db: Session = Depends(get_db)):
    job = db.query(Job).filter(Job.status == "ready").order_by(Job.ready_at.asc()).first()
    if job is not None:
        job.status = "playing"
        job.consumed_at = datetime.utcnow()
        db.commit()
    return {"ok": True}


@router.get("/video/stream/{job_id}.mjpeg")
def stream_mjpeg(job_id: str, tier: str = "high", db: Session = Depends(get_db)):
    job = db.get(Job, job_id)
    if job is None:
        raise HTTPException(status_code=404, detail="stream not available")

    if tier == "low" and job.mjpeg_path_low and os.path.exists(job.mjpeg_path_low):
        path, fps = job.mjpeg_path_low, job.fps_low
    else:
        path, fps = job.mjpeg_path, job.fps

    if not path or not os.path.exists(path):
        raise HTTPException(status_code=404, detail="stream not available")

    def generate():
        yield bytes([fps])
        yield from _file_chunks(path)

    # The ESP32 firmware reads this stream's raw bytes directly via
    # HTTPClient::getStreamPtr() (video_player.cpp), which does NOT decode
    # chunked transfer-encoding — without an explicit Content-Length here,
    # uvicorn falls back to chunked framing and the firmware ends up reading
    # literal chunk-size/CRLF bytes as if they were stream payload.
    content_length = 1 + os.path.getsize(path)
    return StreamingResponse(
        generate(),
        media_type="video/x-motion-jpeg",
        headers={"Content-Length": str(content_length)},
    )


@router.get("/video/stream/{job_id}.mp3")
def stream_mp3(job_id: str, db: Session = Depends(get_db)):
    job = db.get(Job, job_id)
    if job is None or not job.mp3_path or not os.path.exists(job.mp3_path):
        raise HTTPException(status_code=404, detail="stream not available")

    # Same reasoning as stream_mjpeg above: the firmware's audio task reads
    # this stream's raw bytes directly too, so it needs identity framing,
    # not chunked.
    content_length = os.path.getsize(job.mp3_path)
    return StreamingResponse(
        _file_chunks(job.mp3_path),
        media_type="audio/mpeg",
        headers={"Content-Length": str(content_length)},
    )
