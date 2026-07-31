import os

from fastapi import APIRouter, Depends, HTTPException
from fastapi.responses import FileResponse
from sqlalchemy.orm import Session

from ..auth import require_device_key
from ..db import get_db
from ..models import Photo
from ..schemas import CurrentPhotoOut
from .settings import get_or_create_settings

router = APIRouter(tags=["photo"], dependencies=[Depends(require_device_key)])


@router.get("/photo/current", response_model=CurrentPhotoOut)
def current(db: Session = Depends(get_db)):
    s = get_or_create_settings(db)
    if not s.current_photo_id:
        return CurrentPhotoOut(ready=False)
    return CurrentPhotoOut(ready=True, photo_id=s.current_photo_id)


@router.get("/photo/stream/{photo_id}.jpg")
def stream_photo(photo_id: str, db: Session = Depends(get_db)):
    photo = db.get(Photo, photo_id)
    if photo is None or not os.path.exists(photo.path):
        raise HTTPException(status_code=404, detail="photo not available")

    # FileResponse (not a hand-rolled StreamingResponse) — it sets
    # Content-Length itself since the file's already fully on disk, which
    # is exactly the fix routers/video.py's streams needed after the
    # firmware turned out to be reading raw (non-dechunked) HTTPClient
    # bytes: without Content-Length, uvicorn falls back to chunked
    # transfer-encoding and the device reads literal chunk-framing bytes
    # as if they were payload.
    return FileResponse(photo.path, media_type="image/jpeg")
