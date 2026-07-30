from fastapi import APIRouter, Depends, HTTPException

from ..auth import require_web_session
from ..schemas import SearchResponse
from ..services import ytdlp_service

router = APIRouter(tags=["search"], dependencies=[Depends(require_web_session)])


@router.get("/api/search", response_model=SearchResponse)
async def search(q: str):
    q = q.strip()
    if not q:
        raise HTTPException(status_code=400, detail="q is required")
    results = await ytdlp_service.search(q)
    return SearchResponse(results=results)
