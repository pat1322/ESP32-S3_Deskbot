#pragma once

#include <Arduino.h>

// Returns the ready job's id, or "" if none is ready / on error.
String checkCurrentJob();

// Fetches the human-readable title for a job (best-effort, "" on failure).
String getJobTitle(const String& jobId);

// Tells the server the device has dispatched the current job (moves it
// from ready -> playing server-side) so it won't be reported again.
void clearCurrentJob();

// Fetches the to-do summary. Returns false on network/parse failure,
// leaving pendingCount/nextTask untouched.
bool getTodoSummary(int& pendingCount, String& nextTask);
