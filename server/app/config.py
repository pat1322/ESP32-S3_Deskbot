from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    # Device auth (required — the ESP32 sends this in X-Api-Key)
    deskbot_api_key: str = "dev-device-key"

    # Web auth — if unset, the login gate is disabled (dev convenience)
    web_password: str | None = None
    session_secret: str = "dev-insecure-secret-change-me"

    # Database
    database_url: str = "sqlite:///./deskbot.db"

    # Media scratch space
    media_dir: str = "./media"

    # Video pipeline tuning — favors smooth/reliable playback on the
    # ESP32-S3's SPI/JPEG-decode budget over image quality. Lower fps and
    # higher jpeg_q (more compression) both shrink per-frame decode time
    # and network bandwidth, which is what actually keeps the device from
    # falling behind and dropping frames.
    default_fps: int = 10
    jpeg_q: int = 10
    max_job_duration_s: int = 20 * 60
    ready_grace_period_s: int = 15 * 60
    error_grace_period_s: int = 5 * 60
    cleanup_interval_s: int = 120

    # Search
    search_result_limit: int = 12


settings = Settings()
