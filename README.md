# Deskbot

A mini desk gadget built on an ESP32-S3: an animated idle clock, a YouTube
video player (or your own uploaded videos/photos), a to-do list, rotating
idle-screen quotes, and a focus/Pomodoro timer — all driven from a
companion website.

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
`VideoTester.ino` already proved out. You can queue up several videos at
once — they play back-to-back in order ("Up next" on the website shows
what's waiting). When nothing is queued, the device shows an animated
clock/date idle screen with a small to-do summary and a rotating short
quote, both editable from the website — or, if a focus session is
running, a countdown timer instead.

The website has a top nav bar (collapsing to a hamburger menu on mobile)
with four tabs: **YouTube** (search, now playing/up next with playback
controls, results), **Upload** (your own video/photo, camera capture),
**Tasks** (focus timer, to-do), and **Settings** (network, background
theme, idle quotes, device log). A "Live preview" in the sidebar mirrors
the device's actual idle-screen layout. Both the website and the physical
device open with a short branded animation (see "Boot & splash animation"
below).

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

Audio plays over a separate stream with no shared clock, so it gets the
same kind of catch-up ability: if it falls more than
`AUDIO_RESYNC_THRESHOLD_MS` behind real time (the video stream eating most
of the WiFi throughput is the usual cause), it skips ahead in the audio
instead of letting the gap grow into lip-sync drift for the rest of the
video — watch for `[Audio] Resyncing, N ms behind` in the serial log.

### Playback controls

The YouTube tab's "Now playing" panel doubles as a mini player: a **Next**
button (enabled once something else is queued) skips the current video —
under the hood it's the same cancel-and-advance mechanism as Cancel, just
one click away — and a **volume** slider, persisted server-side and
applied on the device's next poll (~45s), no reflash needed. Pause/
resume/seek aren't supported yet — the device has no pause primitive or
server-side seek today.

### Boot & splash animation

The device plays a short (~2s) branded animation on boot — three dots
orbit in and settle onto a ring, then the "DESKBOT" wordmark and a
"Developed by Patrick Perez" credit line fade in — drawn with `TFT_eSPI`
primitives, no image assets. The website shows a matching animated splash
(SVG + CSS) on every page load, held for a minimum time and until the
login/session check resolves, whichever is longer, so it never flashes
away before there's real content underneath it.

### Orientation

Switch between landscape and portrait from the Settings tab's "Display"
panel — meant for a Deskbot enclosure you can physically turn sideways,
not a fixed-mount one. Applies on the device's next poll while idle or in
a focus session (never mid-video). Landscape content wider than a portrait
screen (240px instead of 320px) is center-cropped, not scaled down.

### Focus timer

Start a countdown (a preset or a custom minute count) from the Tasks tab's
"Focus timer" panel; the device switches its screen to a big countdown
until it ends or you stop it early, then returns to idle. It doesn't
interrupt a video that's already playing, and doesn't itself get
interrupted by a newly-queued video — that just waits until the session
ends.

### Uploading your own video or photo

The Upload tab's "Upload media" panel lets you play your own video files or
show a photo, no YouTube involved:

- **Video** goes through the same pipeline as a searched video (encoded
  into the same dual-tier MJPEG+MP3 format, same "Now Playing"/"Up Next"
  queue — uploads and searches interleave in the same playlist, badged
  "Upload" so you can tell them apart) — capped at `MAX_UPLOAD_VIDEO_MB`
  (default 300MB) and the same `MAX_JOB_DURATION_S` limit as YouTube
  videos.
- **Photo** resizes/crops to the device's screen and displays immediately,
  persisting until you upload another or hit Dismiss — capped at
  `MAX_UPLOAD_PHOTO_MB` (default 20MB). Doesn't interrupt a playing video
  or an active focus session; if either is active when you upload a
  photo, it just waits its turn.

You can also capture a photo directly from your webcam or phone camera —
"Use camera" opens a live preview in the browser (needs camera
permission), "Capture" grabs the current frame and uploads it exactly
like a picked file.

Either way, pick a filter before uploading/capturing: **black & white**
and **cinematic** are instant color grades; **anime** runs the photo
through a small pretrained style-transfer model (CPU, no GPU needed,
roughly a second on typical hardware) — it's trained on faces, so it
suits webcam selfies better than landscape photos. See `CLAUDE.md` for
where that model came from and its license.

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

- You can queue up multiple videos — they play back-to-back in the order
  queued ("Up next" on the website shows what's waiting). Only one job
  downloads/encodes/plays at a time either way; queuing further ahead just
  lets the next one start converting while the current one's still
  playing. Capped at 10 queued-at-once as a sanity limit.
- Videos over ~20 minutes are rejected by default (`MAX_JOB_DURATION_S`) to
  bound worst-case CPU/disk use on a small Railway instance.
- `yt-dlp` occasionally breaks when YouTube changes things; if search/queue
  starts failing, try bumping the `yt-dlp` version in `requirements.txt`.
- If a queued video's error message says something like *"Sign in to
  confirm you're not a bot"*, that's YouTube bot-checking Railway's
  datacenter IP rather than an app bug — set `YTDLP_COOKIES` (see
  `.env.example`) to authenticate `yt-dlp` with a real browser session.
- If downloads fail with *"Requested format is not available"*, that's
  YouTube's "n challenge" anti-bot signature failing to solve — `yt-dlp`
  needs both `yt-dlp[default]` (installs the `yt-dlp-ejs` solver scripts,
  already in `requirements.txt`) and a JS runtime on `PATH` (the Dockerfile
  copies in a Deno binary for this). Local dev without Deno installed will
  hit the same error; install Deno or run search-only features.
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
