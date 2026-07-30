#include "wifi_store.h"

#include <Preferences.h>

static const char* NVS_NAMESPACE = "wifi";

bool wifiStoreLoad(String& ssid, String& pass) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
    String storedSsid = prefs.getString("ssid", "");
    String storedPass = prefs.getString("pass", "");
    prefs.end();

    if (storedSsid.length() == 0) return false;
    ssid = storedSsid;
    pass = storedPass;
    return true;
}

void wifiStoreSave(const String& ssid, const String& pass) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
}
