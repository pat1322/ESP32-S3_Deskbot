# CLAUDE.md

Architecture notes for working on Deskbot with AI assistance. Read this
before making non-trivial changes — it explains the *why* behind a few
non-obvious design choices that aren't visible from any single file.

## What this is

An ESP32-S3 desk gadget (idle clock / YouTube video player / to-do list)
driven by a FastAPI backend + single-page website. See `README.md` for
setup instructions; this file is about how the pieces fit together.

```
firmware/Deskbot/   ESP32-S3 Arduino sketch
server/app/         FastAPI backend + static website
VideoTester/         original proof-of-concept (untouched, reference only)
```

## Where to look for each kind of change

- Adding a device-facing setting (like `bg_theme`/`volume`): `models.py`
  (`Settings` table) → `schemas.py` (`SettingsOut`/`SettingsPatch`/
  `DeviceStateOut`) → `routers/settings.py` (web PATCH) →
  `routers/device.py` (device poll) → website (`index.html`/`app.js`/
  `style.css`) → firmware (`api_client.h`/`.cpp`'s `DeviceState` struct,
  `Deskbot.ino`'s `handleDeviceStatePoll()`).
- Adding a video-pipeline change: `services/ffmpeg_service.py` (encode) →
  `services/job_worker.py` (orchestration) → `routers/video.py` (stream
  endpoint) → `firmware/Deskbot/src/video_player.cpp` (playback).
- Firmware screen states: `firmware/Deskbot/src/state_machine.h`/`.cpp`
  (`AppState` enum) is the single source of truth for what the device is
  doing; `Deskbot.ino`'s `loop()` switches on it.

## Auth model

Two independent credentials, both checked in `server/app/auth.py`:

- **`X-Api-Key` header** (`require_device_key`) — the ESP32 firmware only.
  Constant-time compared against `DESKBOT_API_KEY`.
- **Session cookie** (`require_web_session`) — the website only, gated on
  `WEB_PASSWORD` being set (unset = auth disabled, for local dev).
- **`require_device_or_web`** — endpoints the device polls that also need
  to be readable from the website for debugging (`/device/state`).

This is deliberately a single-owner, shared-secret model — no per-user
accounts. See `SECURITY.md` for the full list of accepted trade-offs.

## Job lifecycle (video pipeline)

`Job.status` progression, defined in `server/app/models.py`:

```
queued → downloading → encoding → ready → playing → done
                                        \-> error
                                        \-> cancelled
```

Multiple jobs can be queued at once (a playlist) — `POST /api/queue` only
409s past `routers/queue.py`'s `MAX_QUEUE_DEPTH` sanity cap, not on a
single active job. `services/job_worker.py`'s `_worker_loop` already
processed its `asyncio.Queue` strictly FIFO even before this, so nothing
about download/encode concurrency changed — only the queue-time rejection
did. The device still only ever plays one job at a time: `/video/current`
returns the single oldest `"ready"` job, and the done-signal (above) makes
the next one visible the moment the current one finishes.
`services/job_worker.py`'s `process_job()` drives the whole pipeline:
download via `yt-dlp` → encode via `ffmpeg` (see below) → mark `ready`. The
device polls `GET /video/current` for a ready job, then
`POST /video/current/clear` flips it to `playing` so it isn't reported
twice. `services/cleanup.py` sweeps stale `ready`/`playing`/`error`/
`cancelled` jobs on a timer, deleting their media — this is only a
fallback net, not the primary way `playing` resolves (see below).

**Ending a `playing` job promptly.** The device has no way to push
"finished" the instant it happens other than telling the server itself:
`video_player.cpp`'s `playVideo()` calls `POST /video/current/done` right
after it returns (success, stall, or cancel — doesn't matter which), which
flips `playing` → `done` immediately. Without this, `playing` would only
resolve via `cleanup.py`'s sweep, up to `ready_grace_period_s` (15 min)
later — which is exactly what happens if the device never got to send it
(crash, power loss, etc.), so the sweep still matters as a backstop.

**`cancelled` vs `error`.** A user-initiated cancel (`POST
/api/jobs/{id}/cancel`) always sets `cancelled`, not `error` — the website
treats them differently (`cancelled` clears straight back to "Idle";
`error` shows a Retry button). If the job was already `playing` on the
device, cancelling only updates the DB — the device finds out via its own
polling: `playVideo()`'s decode loop calls `GET /video/status/{id}` every
`VIDEO_CANCEL_CHECK_MS` (`pins.h`) and stops within a few seconds once the
status is no longer `"playing"`.

**Dual-tier encoding**: every job produces *two* MJPEG files at encode
time — `mjpeg_path` ("high", `settings.default_fps`/`jpeg_q`) and
`mjpeg_path_low` ("low", `settings.low_fps`/`low_jpeg_q`) — generated in
the same `asyncio.gather()` alongside the MP3 extraction. This exists so
the device can fall back to a cheaper stream without an on-demand
re-encode; see `routers/video.py`'s `tier` query param and the firmware
section below. The source video is deleted immediately after all three
outputs finish, so **there is no way to re-encode a job after the fact** —
if you need a third tier or different tuning, it has to happen before that
deletion.

**No DB migrations.** `server/app/db.py`'s `sync_schema()` is a hand-rolled
substitute for Alembic: `Base.metadata.create_all()` (creates missing
tables) followed by an `inspect()`-driven pass that adds any columns
present in the SQLAlchemy models but missing from the live table (runs on
every startup, idempotent). New columns are always added nullable at the
DB level regardless of the model's declared nullability, since existing
rows have nothing to backfill. If you add a column to `Job` or `Settings`,
you don't need to write a migration — this handles it. If you ever
introduce a genuinely destructive schema change (renaming/dropping a
column, changing a type), this shim won't help — you're on your own for
that one.

**Corollary: a non-nullable new column needs a read-time coalesce.** A
column added this way is nullable at the DB level no matter what the
model/response-schema declare, so an existing row (created before that
column existed) reads back as `None` for it — which then fails Pydantic
response validation if the schema field isn't `Optional`. `routers/
settings.py`'s `get_or_create_settings()` is the pattern: coalesce `None`
→ the intended default once, right after fetching, before anything reads
the row (see `focus_active`/`focus_label` there for a worked example).
This bit real production data the first time a column was added to an
already-existing table — `bg_theme`/`volume`/etc. never hit it because
they were part of `Settings`'s original schema, not added later.

## Firmware playback pacing (the part most likely to bite you)

`firmware/Deskbot/src/video_player.cpp`'s `playVideo()` is a for-loop over
**attempts** (max 3, but two independent one-shot flags mean it never
actually reaches 3 in practice):

1. Pick a starting tier from `WiFi.RSSI()` (a cheap proxy — real
   throughput isn't known until something is already streaming).
2. Pre-fill the JPEG ring buffer; if throughput measured during that fill
   is too low for the high tier and this hasn't been tried yet, restart at
   the low tier before a single frame is drawn (invisible to the user).
3. Decode/draw loop: paces frames against wall-clock time, but — this is
   the fix for the old "stuck on one frame" bug — **never lets more than
   `VIDEO_FORCE_DRAW_MAX_SKIP` consecutive frames go undrawn**, even when
   badly behind schedule. Without this, sustained low throughput makes
   `elapsedMs` permanently outpace the pacing target and the screen simply
   stops updating forever. A rolling skip-ratio over each 5s window also
   triggers one mid-stream downgrade-and-restart (from the top — there's
   no server-side seek) if it's consistently bad on the high tier.

Tuning constants (`VIDEO_PREFILL_MIN_BPS_HIGH`, `VIDEO_SKIP_RATIO_DOWNGRADE`,
`VIDEO_FORCE_DRAW_MAX_SKIP`) live in `firmware/Deskbot/src/pins.h` and are
starting points, not measured values — tune them against real WiFi using
the serial log (`[Video] N fps, N% skipped`).

A second FreeRTOS task (`audioTaskFn`, pinned to core 0) streams/decodes
audio in parallel. `g_audioTaskRunning` + `waitForAudioTaskTeardown()`
exist specifically to prevent two audio tasks ever running concurrently
across a tier-downgrade restart — both would otherwise drive the same
static `i2sCodec`/`decoded` objects.

## WiFi credential flow

There is no compile-time WiFi credential anymore — `firmware/Deskbot/src/
wifi_store.h`/`.cpp` (NVS via `Preferences`) is the single source of
truth. `config.h`'s `WIFI_SSID_CFG`/`WIFI_PASS_CFG` are a **first-boot seed
only** (`Deskbot.ino`'s `setup()` copies them into NVS once, if NVS is
empty, then never reads them again).

Two ways credentials get (re)written, designed to compose without
knowing about each other:

1. **On-device captive portal** (`wifi_portal.cpp`) — triggered after
   `WIFI_FAIL_PORTAL_THRESHOLD` consecutive connect failures in `loop()`'s
   `WIFI_CONNECT` state, or effectively on first boot if the seeded
   credentials don't work. Opens an open SoftAP, a DNS server that
   resolves every query to itself (triggers the OS's captive-portal
   popup), and a one-page form. Always `ESP.restart()`s on submit —
   success boots into the new network, failure boots back into the normal
   retry loop, which reopens the portal after the threshold again. No
   separate in-portal retry logic needed.
2. **Website live-switch** — `Settings.pending_wifi_ssid`/
   `pending_wifi_password`/`wifi_apply_status` on the server, set by
   `POST /api/settings/wifi`, picked up by the device on its normal
   `handleDeviceStatePoll()` (~45s cadence), applied via
   `applyPendingWifi()` in `Deskbot.ino`. The old NVS credentials are left
   alone until the new network proves it connects; on failure the device
   reconnects to the old network and reports `failed` back
   (`POST /device/wifi/ack`) over that restored connection. If the *old*
   network is also gone (device physically moved), this just falls
   through to the normal `WIFI_CONNECT` retry loop → captive portal.

## Things that look like bugs but aren't

- `SettingsPatch` fields are all `Optional` (partial-update PATCH
  semantics) — don't "fix" this back to required fields, the theme picker
  and volume slider each PATCH only their own field.
- `stream_mjpeg`'s `tier` param silently falls back to `"high"` if `"low"`
  is requested but not ready/missing — this is intentional, not a
  swallowed error.
