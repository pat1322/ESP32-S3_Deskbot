# Deskbot

A mini desk gadget built on an ESP32-S3: an animated idle clock, a YouTube
video player, and a to-do list — all driven from a companion website.

- **`firmware/Deskbot/`** — the ESP32-S3 sketch (idle clock + video playback)
- **`server/`** — Python/FastAPI backend + the website (search, queue, to-do list)
- **`VideoTester/`** — the original proof-of-concept this project is based on (untouched, kept for reference)

## How it works

The device polls the backend every few seconds for a queued video job and a
to-do summary. When you queue a video from the website, the backend downloads
it with `yt-dlp` and transcodes it (via `ffmpeg`) into a raw MJPEG frame
stream + an MP3 audio track, matching exactly what the firmware expects. The
device streams both over HTTP, decoding JPEG frames with `JPEGDEC` and audio
through the ES8311 codec via `AudioTools`, the same pipeline `VideoTester.ino`
already proved out. When nothing is queued, the device shows an animated
clock/date idle screen with a small to-do summary underneath.

## Firmware setup

1. Install the Arduino IDE (2.x) with ESP32 board support.
2. Install libraries: `TFT_eSPI`, `JPEGDEC`, `ArduinoJson`, `AudioTools`
   (with the ES8311 codec driver) — same set `VideoTester.ino` uses. Your
   `TFT_eSPI` `User_Setup.h` should already be configured for this board's
   display pins; this project doesn't touch that.
3. Copy `firmware/Deskbot/config.h.example` to `firmware/Deskbot/config.h`
   and fill in your WiFi credentials, your deployed backend URL, the
   `DESKBOT_API_KEY` (must match the server's), and your timezone offset.
4. Open `firmware/Deskbot/Deskbot.ino`, select your ESP32-S3 board, and
   flash. Watch the serial monitor at boot — it logs WiFi connect status
   and free/total PSRAM, useful for confirming headroom on your specific
   board.

Wiring is identical to `VideoTester.ino` — same I2C/I2S pins for the ES8311
codec, same display orientation (`rotation(1)`, 320×240 landscape).

## Backend setup (local dev)

```
cd server
python -m venv .venv && .venv\Scripts\activate   # Windows
pip install -r requirements.txt
copy .env.example .env      # then edit values
uvicorn app.main:app --reload
```

Open `http://localhost:8000`. With `WEB_PASSWORD` unset in `.env`, the login
gate is skipped for local dev.

**Before wiring up real hardware**, validate the MJPEG pipeline once:
queue a short video locally, then byte-scan the resulting
`server/app/media/<job_id>/video.mjpeg` for `0xFFD8`/`0xFFD9` frame
boundaries (the same logic the firmware uses) to confirm frame counts and
sizes look sane before trusting it to a live stream.

## Deploying to Railway

1. Push this repo to GitHub.
2. Create a **new** Railway project (this is a fresh project, unrelated to
   any prior backend), connect the GitHub repo, and let it build
   `server/Dockerfile`.
3. Add a **Postgres** plugin to the project — Railway injects `DATABASE_URL`
   automatically.
4. Set environment variables on the Railway service: `DESKBOT_API_KEY`,
   `WEB_PASSWORD`, `SESSION_SECRET` (long random string), and optionally
   `MEDIA_DIR`, `DEFAULT_FPS`, `MAX_JOB_DURATION_S`.
5. Once deployed, point `firmware/Deskbot/config.h`'s `DESKBOT_SERVER_URL`
   at the Railway URL and reflash.

No persistent Railway volume is needed — downloaded/transcoded media is
scratch space that gets cleaned up automatically after each job is consumed.

## Notes and known trade-offs

- Only one video job is processed at a time — queuing a second video while
  one is in progress returns a 409 until the first finishes.
- Videos over ~20 minutes are rejected by default (`MAX_JOB_DURATION_S`) to
  bound worst-case CPU/disk use on a small Railway instance.
- The website uses a single shared password, not per-user accounts — this
  is a personal single-owner gadget, not a multi-tenant service.
- Downloading YouTube videos via `yt-dlp` is against YouTube's Terms of
  Service. This project is intended for personal, single-device use with no
  redistribution — media is deleted from the server shortly after playback.
- `yt-dlp` occasionally breaks when YouTube changes things; if search/queue
  starts failing, try bumping the `yt-dlp` version in `requirements.txt`.
