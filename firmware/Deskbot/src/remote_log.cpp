#include "remote_log.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <stdarg.h>
#include <string.h>

#include "../config.h"

static const int LOG_LINE_MAX          = 192;
static const int MAX_QUEUED_LINES  = 30;

static char queuedLines[MAX_QUEUED_LINES][LOG_LINE_MAX];
static int  queuedCount = 0;

static String serverBase() {
    String base = String(DESKBOT_SERVER_URL);
    while (base.endsWith("/")) base.remove(base.length() - 1);
    return base;
}

void remoteLog(const char* fmt, ...) {
    char buf[LOG_LINE_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    Serial.println(buf);

    if (queuedCount >= MAX_QUEUED_LINES) {
        // drop the oldest line to make room rather than growing unbounded —
        // the array is contiguous, so this shifts every row down by one.
        memmove(queuedLines[0], queuedLines[1], (MAX_QUEUED_LINES - 1) * LOG_LINE_MAX);
        queuedCount = MAX_QUEUED_LINES - 1;
    }
    strncpy(queuedLines[queuedCount], buf, LOG_LINE_MAX - 1);
    queuedLines[queuedCount][LOG_LINE_MAX - 1] = '\0';
    queuedCount++;
}

void remoteLogFlush() {
    if (queuedCount == 0 || WiFi.status() != WL_CONNECTED) return;

    String body;
    body.reserve(queuedCount * 64);
    for (int i = 0; i < queuedCount; i++) {
        body += queuedLines[i];
        body += '\n';
    }
    int sentCount = queuedCount;

    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setConnectionTimeout(5000);
    HTTPClient http;
    http.begin(cli, serverBase() + "/device/log");
    http.addHeader("X-Api-Key", DESKBOT_API_KEY);
    http.addHeader("Content-Type", "text/plain");
    http.setTimeout(5000);
    int code = http.POST(body);
    http.end();

    if (code == 200) {
        // Only drop what we actually sent — remoteLog() calls made while
        // this POST was in flight (e.g. from other code between the body
        // being built and the request completing) would otherwise be lost.
        int remaining = queuedCount - sentCount;
        if (remaining > 0) {
            memmove(queuedLines[0], queuedLines[sentCount], remaining * LOG_LINE_MAX);
        }
        queuedCount = remaining;
    }
    // on failure, leave the queue in place; the next call retries (bounded
    // by MAX_QUEUED_LINES, so a long outage just drops the oldest context)
}
