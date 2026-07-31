#pragma once

#include <Arduino.h>

// Fetches /photo/stream/{photoId}.jpg and decodes+draws it once, center-
// cropped the same way video_player.cpp's frames are. Call once when
// entering AppState::PHOTO_VIEW (mirrors idle_screen.h/focus_timer.h's
// Enter() pattern) — on any failure, falls back to showScreen()'s generic
// error display rather than leaving a half-drawn frame on screen.
void photoViewEnter(const String& photoId);

// Non-blocking — call every loop() iteration while in PHOTO_VIEW. The
// photo is a static image (no per-tick redraw needed), so this exists
// only for loop() consistency with idleScreenTick()/focusTimerTick().
void photoViewTick();
