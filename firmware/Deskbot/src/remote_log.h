#pragma once

#include <Arduino.h>

// Alternative to a physical serial monitor: mirrors output to Serial
// (unchanged local debugging) and queues it in an in-RAM buffer that
// remoteLogFlush() periodically POSTs to the backend's /device/log, where
// it's viewable live from the website's "Device Log" panel.
//
// Core-affinity note: this module keeps no lock around its internal
// buffer, so it's only safe to call from the main Arduino task (core 1) —
// setup()/loop(), Deskbot.ino, video_player.cpp's playVideo(),
// network.cpp, wifi_portal.cpp. Do NOT call remoteLog() or
// remoteLogFlush() from audioTaskFn (pinned to core 0) — plain
// Serial.print() there is still fine.
void remoteLog(const char* fmt, ...);

// POSTs any queued lines to the backend and clears the queue on success.
// No-ops if there's nothing queued or WiFi isn't connected. Safe (and
// intended) to call frequently — e.g. every few seconds, and right after
// any event worth seeing promptly.
void remoteLogFlush();
