from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ..auth import require_web_session
from ..db import get_db
from ..models import Quote
from ..schemas import QuoteIn, QuoteOut

router = APIRouter(tags=["quotes"], dependencies=[Depends(require_web_session)])


@router.get("/api/quotes", response_model=list[QuoteOut])
def list_quotes(db: Session = Depends(get_db)):
    return db.query(Quote).order_by(Quote.created_at.asc()).all()


@router.post("/api/quotes", response_model=QuoteOut)
def create_quote(body: QuoteIn, db: Session = Depends(get_db)):
    text = body.text.strip()
    if not text:
        raise HTTPException(status_code=400, detail="text is required")
    quote = Quote(text=text[:64])
    db.add(quote)
    db.commit()
    db.refresh(quote)
    return quote


@router.delete("/api/quotes/{quote_id}")
def delete_quote(quote_id: int, db: Session = Depends(get_db)):
    quote = db.get(Quote, quote_id)
    if quote is None:
        raise HTTPException(status_code=404, detail="quote not found")
    db.delete(quote)
    db.commit()
    return {"ok": True}
