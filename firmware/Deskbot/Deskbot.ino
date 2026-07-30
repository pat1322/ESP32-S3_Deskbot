#include "config.h"

#include <WiFi.h>

#include "src/pins.h"
#include "src/display.h"
#include "src/network.h"
#include "src/api_client.h"
#include "src/video_player.h"
#include "src/idle_screen.h"
#include "src/state_machine.h"

static uint32_t lastJobPollMs    = 0;
static uint32_t lastDeviceStateMs = 0;

static void handleVideoJobIfAny() {
    if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); return; }

    String jobId = checkCurrentJob();
    if (jobId.length() == 0) return;

    Serial.printf("[Poll] Job: %s\n", jobId.c_str());
    String title = getJobTitle(jobId);
    String shortTitle = title;
    if (shortTitle.length() > 26) shortTitle = shortTitle.substring(0, 23) + "...";
    clearCurrentJob();

    transitionTo(AppState::VIDEO_STARTING);
    showScreen("Starting...", shortTitle.length() > 0 ? shortTitle.c_str() : jobId.c_str());

    transitionTo(AppState::VIDEO_PLAYING);
    if (!playVideo(jobId, title)) {
        showScreen("Retrying...", "");
        delay(1500);
        playVideo(jobId, title);
    }

    transitionTo(AppState::IDLE);
    lastJobPollMs = millis();
}

static void handleDeviceStatePoll() {
    if (WiFi.status() != WL_CONNECTED) return;
    int pending;
    String nextTask, bgTheme;
    if (getDeviceState(pending, nextTask, bgTheme)) {
        idleScreenSetTodoSummary(pending, nextTask);
        idleScreenSetTheme(bgTheme);
    }
}

void setup() {
    Serial.begin(115200);
    delay(400);

    displayInit();
    transitionTo(AppState::BOOT);

    initVideoSubsystem();

    transitionTo(AppState::WIFI_CONNECT);
    if (connectWiFi()) {
        Serial.printf("[WiFi] %s\n", WiFi.localIP().toString().c_str());
        initNTP();
        transitionTo(AppState::IDLE);
    }
    // else: stay in WIFI_CONNECT, loop() will keep retrying.
}

void loop() {
    switch (g_appState) {
        case AppState::BOOT:
            transitionTo(AppState::WIFI_CONNECT);
            break;

        case AppState::WIFI_CONNECT:
            if (connectWiFi(8000)) {
                Serial.printf("[WiFi] %s\n", WiFi.localIP().toString().c_str());
                initNTP();
                transitionTo(AppState::IDLE);
            } else {
                showScreen("WiFi FAILED", "Retrying...");
                delay(2000);
            }
            break;

        case AppState::IDLE: {
            idleScreenTick();

            uint32_t now = millis();
            if (now - lastJobPollMs >= JOB_POLL_INTERVAL_MS) {
                lastJobPollMs = now;
                handleVideoJobIfAny();
            }
            if (now - lastDeviceStateMs >= DEVICE_STATE_POLL_INTERVAL_MS) {
                lastDeviceStateMs = now;
                handleDeviceStatePoll();
            }
            break;
        }

        case AppState::VIDEO_STARTING:
        case AppState::VIDEO_PLAYING:
            // playVideo() is blocking and returns before g_appState would
            // ever be observed here — nothing to do.
            break;
    }

    yield();
}
