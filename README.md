# Deskbot

A mini desk gadget built on an ESP32-S3: an animated idle clock, a YouTube
video player, and a to-do list — all driven from a companion website.

- **`firmware/Deskbot/`** — the ESP32-S3 sketch (idle clock + video playback)
- **`server/`** — Python/FastAPI backend + the website (search, queue, to-do list)
- **`VideoTester/`** — the original proof-of-concept this project is based on (untouched, kept for reference)

## How it works

The device polls the backend every few seconds for a queued video job and a
to-do summary. When you queue a video from the website, the backend downloads
it with `yt-dlp` and transcodes it (via `ffmpeg`) into **two** MJPEG frame
streams — a default-quality "high" tier and a cheaper "low" tier — plus one
MP3 audio track, matching exactly what the firmware expects. The device picks
whichever tier its connection can actually sustain (see "Adaptive video
quality" below), streams it over HTTP, decodes JPEG frames with `JPEGDEC`,
and plays audio through the ES8311 codec via `AudioTools` — the same pipeline
`VideoTester.ino` already proved out. When nothing is queued, the device
shows an animated clock/date idle screen with a small to-do summary and a
volume slider, both controlled from the website.

### Adaptive video quality

Every queued video is encoded at two quality tiers, so the device can fall
back without an on-demand re-encode. Before playing, it makes a quick guess
from WiFi signal strength, then corrects that guess using throughput actually
measured while buffering; if it falls badly behind mid-video on a weak
connection it restarts once at the lower tier (from the top — there's no
server-side seek). The playback loop also guarantees the screen keeps
advancing even under sustained weak throughput, instead of freezing on one
frame. Tune the thresholds against your own network using the serial log
(`[Video] N fps, N% skipped`):

- Server-side tier settings: `LOW_FPS`, `LOW_JPEG_Q` (env vars, safe
  defaults apply if unset).
- Firmware-side thresholds: `VIDEO_PREFILL_MIN_BPS_HIGH`,
  `VIDEO_SKIP_RATIO_DOWNGRADE`, `VIDEO_FORCE_DRAW_MAX_SKIP` in
  `firmware/Deskbot/src/pins.h`.

### Volume

The website's "Desk Unit" panel has a volume slider next to the background
theme picker. It's persisted server-side and applied on the device's next
poll (~45s) — no reflash needed.

### WiFi setup

The device's WiFi credentials live in on-device flash (NVS), not in
`config.h` — there are two ways to set or change them:

1. **First-time / offline setup**: if the device has no working saved
   network (first boot, or it's failed to connect for about 40 seconds
   straight), it opens its own open WiFi access point, **"Deskbot-Setup"**.
   Connect a phone or laptop to it — your OS should pop up a setup page
   automatically (or open `http://192.168.4.1` manually); pick your network
   from the scanned list (or type it in for hidden networks), enter the
   password, and submit. The device restarts and joins it.
2. **Switching networks while already online**: use the website's "Network"
   panel to submit a new SSID/password. The device picks it up on its next
   check-in and switches over, automatically falling back to its previous
   network (and reporting the failure on the website) if the new one
   doesn't work.

`config.h`'s `WIFI_SSID_CFG`/`WIFI_PASS_CFG` are only used to seed the very
first boot if nothing's been saved yet — after that, they're never read
again, and reflashing with new values here won't change anything.

## Firmware setup

1. Install the Arduino IDE (2.x) with ESP32 board support.
2. Install libraries: `TFT_eSPI`, `JPEGDEC`, `ArduinoJson`, `AudioTools`
   (with the ES8311 codec driver) — same set `VideoTester.ino` uses. Your
   `TFT_eSPI` `User_Setup.h` should already be configured for this board's
   display pins; this project doesn't touch that. WiFi setup (`Preferences`,
   `DNSServer`, `WebServer`) uses only libraries that ship with the ESP32
   Arduino core — nothing extra to install there.
3. Copy `firmware/Deskbot/config.h.example` to `firmware/Deskbot/config.h`
   and fill in a first-boot WiFi SSID/password (see "WiFi setup" above —
   you can also skip this and use the on-device portal instead), your
   deployed backend URL, the `DESKBOT_API_KEY` (must match the server's),
   and your timezone offset.
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
   `MEDIA_DIR`, `DEFAULT_FPS`, `MAX_JOB_DURATION_S`, `LOW_FPS`, `LOW_JPEG_Q`.
   Leave `SESSION_COOKIE_SECURE` unset (defaults to `True`) — Railway serves
   HTTPS.
5. Once deployed, point `firmware/Deskbot/config.h`'s `DESKBOT_SERVER_URL`
   at the Railway URL and reflash.

No persistent Railway volume is needed — downloaded/transcoded media is
scratch space that gets cleaned up automatically after each job is consumed.

## Notes and known trade-offs

- Only one video job is processed at a time — queuing a second video while
  one is in progress returns a 409 until the first finishes.
- Videos over ~20 minutes are rejected by default (`MAX_JOB_DURATION_S`) to
  bound worst-case CPU/disk use on a small Railway instance.
- `yt-dlp` occasionally breaks when YouTube changes things; if search/queue
  starts failing, try bumping the `yt-dlp` version in `requirements.txt`.
- If a queued video's error message says something like *"Sign in to
  confirm you're not a bot"*, that's YouTube bot-checking Railway's
  datacenter IP rather than an app bug — set `YTDLP_COOKIES` (see
  `.env.example`) to authenticate `yt-dlp` with a real browser session.
- See [SECURITY.md](SECURITY.md) for the full list of accepted security
  trade-offs (shared-secret auth, the open WiFi setup access point, the
  `yt-dlp` ToS caveat, etc.) — this is a personal single-owner gadget, not a
  hardened multi-tenant service.

## More docs

- [CLAUDE.md](CLAUDE.md) — architecture notes (job lifecycle, firmware
  state machine, auth model, the adaptive video pacing algorithm, WiFi
  credential flow).
- [CONTRIBUTING.md](CONTRIBUTING.md) — dev environment + PR expectations.
- [SECURITY.md](SECURITY.md) — reporting a vulnerability + accepted
  trade-offs.
