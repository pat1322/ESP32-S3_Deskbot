#include "network.h"

#include <WiFi.h>
#include "wifi_store.h"
#include "../config.h"

bool connectWiFi(uint32_t timeoutMs) {
    String ssid, pass;
    if (!wifiStoreLoad(ssid, pass)) return false; // nothing saved yet — caller should offer the setup portal

    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(300);
        yield();
    }
    return WiFi.status() == WL_CONNECTED;
}

void initNTP() {
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
}

bool getLocalTimeSafe(struct tm& timeinfo) {
    return getLocalTime(&timeinfo, 50);
}
