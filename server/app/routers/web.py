import hmac
import os
import time

from fastapi import APIRouter, Form, HTTPException, Request
from fastapi.responses import HTMLResponse, RedirectResponse

from ..auth import auth_enabled
from ..config import settings

router = APIRouter(tags=["web"], include_in_schema=False)

STATIC_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "static")

# Set once per process, i.e. once per deploy (Railway starts a fresh
# process on every deploy) — used to cache-bust style.css/app.js so a
# deploy's changes show up without anyone needing to manually clear
# their browser cache. Deliberately not touching cookies/localStorage to
# achieve this: those hold the login session, and wiping them would just
# force a re-login on every visit instead of solving the actual problem.
_VERSION = str(int(time.time()))


@router.get("/")
def index():
    with open(os.path.join(STATIC_DIR, "index.html"), encoding="utf-8") as f:
        html = f.read()
    html = html.replace(
        '<link rel="stylesheet" href="/static/css/style.css">',
        f'<link rel="stylesheet" href="/static/css/style.css?v={_VERSION}">',
    )
    html = html.replace(
        '<script src="/static/js/app.js"></script>',
        f'<script src="/static/js/app.js?v={_VERSION}"></script>',
    )
    # No-cache (not no-store) so the browser still revalidates cheaply via
    # a conditional request rather than re-downloading the whole page
    # every time, but never serves a stale copy without checking first.
    return HTMLResponse(html, headers={"Cache-Control": "no-cache"})


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
