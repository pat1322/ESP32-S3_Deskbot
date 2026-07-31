#pragma once

#include <Arduino.h>

#include "pins.h"

// Returns the ready job's id, or "" if none is ready / on error.
String checkCurrentJob();

// Fetches the human-readable title for a job (best-effort, "" on failure).
String getJobTitle(const String& jobId);

// Tells the server the device has dispatched the current job (moves it
// from ready -> playing server-side) so it won't be reported again.
void clearCurrentJob();

// Tells the server playback of this job has ended (however it ended —
// finished, stalled out, or was cancelled mid-stream) so the website's
// "Now Playing" clears promptly instead of waiting up to 15 minutes for
// the server's background cleanup sweep. Call once right after
// playVideo() returns, regardless of its result.
void postVideoDone(const String& jobId);

// Combined device state polled from /device/state: to-do summary, chosen
// background theme, volume, and (while a website-initiated network switch
// is pending) the new WiFi credentials to try.
struct DeviceState {
    int    pendingCount = 0;
    String nextTask;
    String bgTheme = "drift";
    float  volume = VOLUME; // pins.h boot default until the first successful poll
    String pendingWifiSsid;
    String pendingWifiPassword;
};

// Fetches the combined device state. Returns false on network/parse
// failure, leaving `out` untouched.
bool getDeviceState(DeviceState& out);

// Acks a website-initiated WiFi switch attempt so the server can clear the
// pending request and reflect the outcome to the website. status is
// "applied" or "failed".
void postWifiAck(const String& status);
