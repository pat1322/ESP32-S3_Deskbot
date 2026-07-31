#pragma once

#include <Arduino.h>

// Draws static chrome and resets cached state. Call once when entering
// AppState::FOCUS_TIMER (mirrors idle_screen.h's idleScreenEnter()).
void focusTimerEnter(const String& label);

// Non-blocking — call every loop() iteration while in FOCUS_TIMER.
// Internally rate-limits its own redraws to once/second, counting down
// locally between polls from the last value focusTimerSetRemaining() gave
// it (based on elapsed millis()) so the display doesn't sit frozen for
// the ~45s between device-state polls. Never drifts for long: each poll
// re-syncs to the server's authoritative focus_seconds_remaining, which
// is itself always computed fresh from Settings.focus_end_at.
void focusTimerTick();

// Pushes a freshly-polled remaining-seconds value — call from
// handleDeviceStatePoll() whenever DeviceState.focusActive is true.
void focusTimerSetRemaining(int secondsRemaining);

// True once the locally-ticking countdown (see focusTimerTick() above)
// has counted down to zero — Deskbot.ino uses this to return to idle
// immediately rather than waiting for the next poll to notice
// focus_active flipped false server-side.
bool focusTimerExpired();
