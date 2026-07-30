import hmac
import os

from fastapi import APIRouter, Form, HTTPException, Request
from fastapi.responses import FileResponse, RedirectResponse

from ..auth import auth_enabled
from ..config import settings

router = APIRouter(tags=["web"], include_in_schema=False)

STATIC_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "static")


@router.get("/")
def index():
    return FileResponse(os.path.join(STATIC_DIR, "index.html"))


@router.get("/api/auth/status")
def auth_status(request: Request):
    return {
        "auth_enabled": auth_enabled(),
        "authenticated": (not auth_enabled()) or bool(request.session.get("authenticated")),
    }


@router.post("/login")
def login(request: Request, password: str = Form(...)):
    if not auth_enabled() or hmac.compare_digest(password, settings.web_password or ""):
        request.session["authenticated"] = True
        return RedirectResponse(url="/", status_code=303)
    raise HTTPException(status_code=401, detail="incorrect password")


@router.post("/logout")
def logout(request: Request):
    request.session.clear()
    return RedirectResponse(url="/", status_code=303)
