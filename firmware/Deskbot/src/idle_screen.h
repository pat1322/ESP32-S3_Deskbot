#pragma once

#include <Arduino.h>

// Draws static chrome and resets cached state. Call whenever the device
// (re)enters idle mode (boot, and after returning from video playback).
void idleScreenEnter();

// Non-blocking — call every loop() iteration. Internally rate-limits
// itself (clock ~1s, ambient animation ~60ms) so it's cheap to call often.
void idleScreenTick();

// Pushes a freshly-polled to-do summary into the idle screen. Safe to
// call even if the device isn't currently idle (it just updates cached
// state and redraws next time idleScreenEnter()/Tick() runs while idle).
void idleScreenSetTodoSummary(int pendingCount, const String& nextTask);
