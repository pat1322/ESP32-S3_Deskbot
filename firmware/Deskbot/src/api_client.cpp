#include "api_client.h"

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

    StaticJsonDocument<256> doc;
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

bool getTodoSummary(int& pendingCount, String& nextTask) {
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setConnectionTimeout(8000);
    HTTPClient http;
    http.begin(cli, serverBase() + "/todos/summary");
    http.addHeader("X-Api-Key", DESKBOT_API_KEY);
    http.setTimeout(8000);
    int code = http.GET();
    if (code != 200) { http.end(); return false; }
    String resp = http.getString();
    http.end();

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;
    pendingCount = doc["pending_count"] | 0;
    nextTask = doc["next_task"] | "";
    return true;
}
