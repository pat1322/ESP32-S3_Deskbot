#pragma once

// Blocking, ~1.5-2s branded animation: three orbiting dots converge onto
// a ring (matching the website's brand mark), the ring draws itself in,
// then "DESKBOT" and a smaller credit line fade in beneath it. Call once
// from setup(), right after displayInit() and before the state machine
// starts — a manufacturer-logo beat, not part of AppState::BOOT itself,
// so AppState::BOOT's own (currently barely-visible) handling is untouched.
void bootScreenPlay();
