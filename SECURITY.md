# Security Policy

Deskbot is a personal hobby device, not a hardened multi-tenant service.
It's designed for one owner running one device against one backend
deployment. That shapes the trade-offs below — please read them before
filing an issue about something already documented here.

## Reporting a vulnerability

Email **patrickperez1322@gmail.com** with details. This is a single-person
project maintained in spare time, so response times aren't guaranteed, but
reports are read and taken seriously.

Please don't open a public GitHub issue for anything that could let someone
compromise a running deployment (e.g. an auth bypass) — email first.

## Known, accepted trade-offs

These are deliberate design decisions for a single-owner gadget, not
overlooked bugs:

- **Shared-secret auth, not per-user accounts.** The website uses one
  shared password (`WEB_PASSWORD`); the device authenticates with one
  shared API key (`DESKBOT_API_KEY`). There's no user management because
  there's only one user.
- **`yt-dlp` downloads.** Downloading YouTube videos via `yt-dlp` is
  against YouTube's Terms of Service. This project is intended for
  personal, single-device use with no redistribution — media is deleted
  from the server shortly after playback.
- **Open WiFi setup access point.** The on-device "Deskbot-Setup" captive
  portal (used for first-time setup or after repeated connection failures)
  is an *open* network with no password, for lowest setup friction. It's
  only up during the brief setup window, and never carries anything more
  sensitive than the WiFi credentials you're about to submit to it.
- **Pending WiFi password sits briefly in the database.** When you submit
  new WiFi credentials from the website's Network panel, the plaintext
  password is stored server-side until the device's next check-in (up to
  ~45s) applies it and acknowledges, at which point it's cleared. Don't
  reuse a highly sensitive password for your desk unit's WiFi network.
- **TLS certificate validation is disabled on the device.** The ESP32
  firmware uses `WiFiClientSecure::setInsecure()` for its HTTPS calls to
  the backend, trading certificate pinning/validation for simplicity on
  constrained hardware. Traffic is still encrypted in transit; it's just
  not validating the server's identity against a CA chain.

## Supported versions

There's no formal release/support cycle — this is a single rolling
`main` branch. Security fixes land there.
