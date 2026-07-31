import os
from contextlib import asynccontextmanager

from fastapi import FastAPI
from starlette.middleware.sessions import SessionMiddleware
from starlette.staticfiles import StaticFiles

from .config import settings
from .db import sync_schema
from .routers import device, queue, quotes, search, settings as settings_router, todos, video, web
from .services import cleanup, job_worker


@asynccontextmanager
async def lifespan(app: FastAPI):
    os.makedirs(settings.media_dir, exist_ok=True)
    sync_schema()

    job_worker.start()
    cleanup.start()
    await job_worker.recover_and_requeue()

    yield

    await job_worker.stop()
    await cleanup.stop()


app = FastAPI(title="Deskbot", lifespan=lifespan)
app.add_middleware(
    SessionMiddleware,
    secret_key=settings.session_secret,
    same_site="lax",
    https_only=settings.session_cookie_secure,
)

app.include_router(video.router)
app.include_router(todos.router)
app.include_router(quotes.router)
app.include_router(search.router)
app.include_router(queue.router)
app.include_router(settings_router.router)
app.include_router(device.router)
app.include_router(web.router)

static_dir = os.path.join(os.path.dirname(__file__), "static")
app.mount("/static", StaticFiles(directory=static_dir), name="static")
