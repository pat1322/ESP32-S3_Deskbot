#include "api_client.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "../config.h"

static String serverBase() {
    String base = String(DESKBOT_SERVER_URL);
    while (base.endsWith("/")) base.remove(base.length() - 1);
    return base;
}

String checkCurrentJob() {
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setConnectionTimeout(8000);
    HTTPClient http;
    http.begin(cli, serverBase() + "/video/current");
    http.addHeader("X-Api-Key", DESKBOT_API_KEY);
    http.setTimeout(8000);
    if (http.GET() != 200) { http.end(); return ""; }
    String resp = http.getString();
    http.end();

    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return "";
    if (!(doc["ready"] | false)) return "";
    return doc["job_id"] | "";
}

String getJobTitle(const String& jobId) {
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setConnectionTimeout(8000);
    HTTPClient http;
    http.begin(cli, serverBase() + "/video/status/" + jobId);
    http.addHeader("X-Api-Key", DESKBOT_API_KEY);
    http.setTimeout(8000);
    if (http.GET() != 200) { http.end(); return ""; }
    String resp = http.getString();
    http.end();

    StaticJsonDocument<384> doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return "";
    return doc["title"] | "";
}

void clearCurrentJob() {
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setConnectionTimeout(8000);
    HTTPClient http;
    http.begin(cli, serverBase() + "/video/current/clear");
    http.addHeader("X-Api-Key", DESKBOT_API_KEY);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(8000);
    http.POST("{}");
    http.end();
}

void postVideoDone(const String& jobId) {
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setConnectionTimeout(8000);
    HTTPClient http;
    http.begin(cli, serverBase() + "/video/current/done");
    http.addHeader("X-Api-Key", DESKBOT_API_KEY);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(8000);
    StaticJsonDocument<96> doc;
    doc["job_id"] = jobId;
    String body;
    serializeJson(doc, body);
    http.POST(body);
    http.end();
}

bool getDeviceState(DeviceState& out) {
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setConnectionTimeout(8000);
    HTTPClient http;
    http.begin(cli, serverBase() + "/device/state");
    http.addHeader("X-Api-Key", DESKBOT_API_KEY);
    http.setTimeout(8000);
    int code = http.GET();
    if (code != 200) { http.end(); return false; }
    String resp = http.getString();
    http.end();

    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;
    out.pendingCount = doc["pending_count"] | 0;
    out.nextTask = doc["next_task"] | "";
    out.bgTheme = doc["bg_theme"] | "drift";
    out.volume = doc["volume"] | VOLUME;
    out.pendingWifiSsid = doc["pending_wifi_ssid"] | "";
    out.pendingWifiPassword = doc["pending_wifi_password"] | "";
    return true;
}

void postWifiAck(const String& status) {
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setConnectionTimeout(8000);
    HTTPClient http;
    http.begin(cli, serverBase() + "/device/wifi/ack");
    http.addHeader("X-Api-Key", DESKBOT_API_KEY);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(8000);
    StaticJsonDocument<64> doc;
    doc["status"] = status;
    String body;
    serializeJson(doc, body);
    http.POST(body);
    http.end();
}
