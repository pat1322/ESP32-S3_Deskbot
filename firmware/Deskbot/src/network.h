#pragma once

#include <Arduino.h>

// Blocking connect with a timeout, mirrors VideoTester.ino's approach.
// Returns true if connected within timeoutMs.
bool connectWiFi(uint32_t timeoutMs = 20000);

// Call once after the first successful WiFi connect. ESP32's SNTP client
// resyncs periodically on its own after this.
void initNTP();

// Fills timeinfo; returns false if time hasn't been synced yet.
bool getLocalTimeSafe(struct tm& timeinfo);
