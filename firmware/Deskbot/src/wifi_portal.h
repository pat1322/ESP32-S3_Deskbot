#pragma once

// Blocking. Brings up the "Deskbot-Setup" open WiFi access point plus a
// DNS-redirect-all captive portal, and serves a scan-and-submit HTML setup
// page (WiFi.scanNetworks() results in a dropdown, a manual-SSID field for
// hidden networks, a password field). On any outcome — successful connect
// or not — it ESP.restart()s the device once credentials are submitted;
// this function does not return in practice.
void wifiPortalRun();
