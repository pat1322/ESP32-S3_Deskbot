#pragma once

#include <Arduino.h>

// Returns the ready job's id, or "" if none is ready / on error.
String checkCurrentJob();

// Fetches the human-readable title for a job (best-effort, "" on failure).
String getJobTitle(const String& jobId);

// Tells the server the device has dispatched the current job (moves it
// from ready -> playing server-side) so it won't be reported again.
void clearCurrentJob();

// Fetches the combined device state (to-do summary + chosen background
// theme). Returns false on network/parse failure, leaving the out
// params untouched.
bool getDeviceState(int& pendingCount, String& nextTask, String& bgTheme);
