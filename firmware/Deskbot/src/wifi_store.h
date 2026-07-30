#pragma once

#include <Arduino.h>

// NVS-backed WiFi credential storage (Preferences library, namespace
// "wifi"). This is the single source of truth for which network the
// device joins — config.h's WIFI_SSID_CFG/WIFI_PASS_CFG are only used to
// seed this on a device's very first boot (see Deskbot.ino's setup()).

// Fills ssid/pass from NVS. Returns false (leaving both untouched) if
// nothing has been saved yet.
bool wifiStoreLoad(String& ssid, String& pass);

// Persists new credentials, overwriting whatever was saved before.
void wifiStoreSave(const String& ssid, const String& pass);
