import hmac

from fastapi import Header, HTTPException, Request, status

from .config import settings


def auth_enabled() -> bool:
    return bool(settings.web_password)


def require_device_key(x_api_key: str | None = Header(default=None)) -> None:
    if not x_api_key or not hmac.compare_digest(x_api_key, settings.deskbot_api_key):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid api key")


def require_web_session(request: Request) -> None:
    if not auth_enabled():
        return
    if not request.session.get("authenticated"):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="not logged in")


def require_device_or_web(request: Request, x_api_key: str | None = Header(default=None)) -> None:
    if x_api_key and hmac.compare_digest(x_api_key, settings.deskbot_api_key):
        return
    if not auth_enabled() or request.session.get("authenticated"):
        return
    raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="unauthorized")
