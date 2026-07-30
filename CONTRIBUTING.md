# Contributing

Deskbot is a personal project, built for one person's desk. PRs and issues
are welcome, but this isn't a project with a roadmap or a review SLA —
expect responses to be occasional rather than prompt.

## Before you start

For anything beyond a trivial fix, open an issue first describing what
you'd like to change. That avoids duplicated effort and lets us agree on
approach before you spend time on an implementation.

## Setting up a dev environment

See the README's "Firmware setup" and "Backend setup (local dev)"
sections — they cover the Arduino toolchain + libraries for the ESP32
firmware, and the Python/FastAPI setup for the backend + website.

Quick backend loop:

```
cd server
python -m venv .venv && .venv\Scripts\activate   # Windows
pip install -r requirements.txt
copy .env.example .env
uvicorn app.main:app --reload
```

The firmware can't be meaningfully tested without real hardware (an
ESP32-S3 with the wiring `VideoTester.ino`/`firmware/Deskbot` expect) —
if you're proposing a firmware change, please describe how you tested it.

## Pull requests

- Keep PRs focused — one change per PR is easier to review than a bundle.
- Explain the *why*, not just the *what*, in the PR description.
- Match the existing code style (no linter is enforced, just follow what's
  around your change).

## Reporting bugs / requesting features

Use the issue templates under `.github/ISSUE_TEMPLATE/`. For anything
security-sensitive, see [SECURITY.md](SECURITY.md) instead of opening a
public issue.
