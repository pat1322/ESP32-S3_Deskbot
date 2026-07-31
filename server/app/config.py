from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    # Device auth (required — the ESP32 sends this in X-Api-Key)
    deskbot_api_key: str = "dev-device-key"

    # Web auth — if unset, the login gate is disabled (dev convenience)
    web_password: str | None = None
    session_secret: str = "dev-insecure-secret-change-me"
    # Marks the session cookie Secure (HTTPS-only). Leave True in production
    # (Railway serves HTTPS); set False only for local plain-http dev.
    session_cookie_secure: bool = True

    # Database
    database_url: str = "sqlite:///./deskbot.db"

    # Media scratch space
    media_dir: str = "./media"

    # Video pipeline tuning — favors smooth/reliable playback on the
    # ESP32-S3's SPI/JPEG-decode budget over image quality. Lower fps and
    # higher jpeg_q (more compression) both shrink per-frame decode time
    # and network bandwidth, which is what actually keeps the device from
    # falling behind and dropping frames.
    default_fps: int = 12
    jpeg_q: int = 16
    # "Low" tier: a second, cheaper-to-stream MJPEG encoded alongside the
    # default ("high") one for every job, so the device can fall back to it
    # on a weak/slow connection without re-encoding on demand. Same 320x240
    # frame size as the high tier — only fps and JPEG compression drop.
    low_fps: int = 10
    low_jpeg_q: int = 20
    max_job_duration_s: int = 20 * 60
    ready_grace_period_s: int = 15 * 60
    error_grace_period_s: int = 5 * 60
    cleanup_interval_s: int = 120

    # Search
    search_result_limit: int = 12

    # Upload limits (routers/upload.py) — enforced by counting bytes as
    # the upload streams to disk, not by trusting the Content-Length
    # header. Photos get their own much smaller cap since the resized
    # output is tiny regardless of input size.
    max_upload_video_mb: int = 300
    max_upload_photo_mb: int = 20

    # Full contents of a Netscape-format cookies.txt exported from a real,
    # logged-in YouTube browser session. Optional, but YouTube frequently
    # bot-checks and rejects requests from datacenter IPs (like Railway's)
    # without it ("Sign in to confirm you're not a bot"). See README.
    ytdlp_cookies: str | None = None


settings = Settings()
