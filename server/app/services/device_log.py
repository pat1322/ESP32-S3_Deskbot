"""In-memory ring buffer for device-pushed log lines.

A debugging aid for when the ESP32's physical serial monitor isn't usable —
the firmware POSTs its log lines here (see /device/log) and the website
polls them back out (see /api/device/log). Deliberately not persisted: this
resets on every server restart/deploy, which is fine for a live-debugging
tool and avoids needing a DB migration for what's essentially scratch data.
"""

from collections import deque
from datetime import datetime

_MAX_LINES = 500
_MAX_LINE_LENGTH = 300

_log: deque[dict] = deque(maxlen=_MAX_LINES)


def append(lines: list[str]) -> None:
    ts = datetime.utcnow().isoformat(timespec="seconds") + "Z"
    for line in lines:
        line = line.strip()[:_MAX_LINE_LENGTH]
        if line:
            _log.append({"ts": ts, "line": line})


def recent() -> list[dict]:
    return list(_log)


def clear() -> None:
    _log.clear()
