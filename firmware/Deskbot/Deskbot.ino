#include "config.h"

#include <WiFi.h>
#include <esp_system.h>

#include "src/pins.h"
#include "src/display.h"
#include "src/network.h"
#include "src/api_client.h"
#include "src/video_player.h"
#include "src/idle_screen.h"
#include "src/focus_timer.h"
#include "src/state_machine.h"
#include "src/wifi_store.h"
#include "src/wifi_portal.h"
#include "src/remote_log.h"

static uint32_t lastJobPollMs     = 0;
static uint32_t lastDeviceStateMs = 0;
static uint32_t lastLogFlushMs    = 0;
static int      wifiFailCount     = 0;

// ~40s of retrying (8s timeout x 5) before offering on-device setup mode —
// long enough to ride out a router reboot, short enough not to strand the
// device if it's genuinely lost its network.
static const int WIFI_FAIL_PORTAL_THRESHOLD = 5;

static const char* resetReasonStr(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "external reset";
        case ESP_RST_SW:        return "software reset (ESP.restart)";
        case ESP_RST_PANIC:     return "PANIC / exception crash";
        case ESP_RST_INT_WDT:   return "interrupt watchdog timeout";
        case ESP_RST_TASK_WDT:  return "task watchdog timeout";
        case ESP_RST_WDT:       return "other watchdog timeout";
        case ESP_RST_DEEPSLEEP: return "deep sleep wake";
        case ESP_RST_BROWNOUT:  return "brownout (power dip)";
        case ESP_RST_SDIO:      return "SDIO reset";
        default:                return "unknown";
    }
}

static void handleVideoJobIfAny() {
    if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); return; }

    String jobId = checkCurrentJob();
    if (jobId.length() == 0) return;

    remoteLog("[Poll] Job: %s", jobId.c_str());
    remoteLogFlush(); // push this now — if playVideo() crashes, we want this on record
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

    // However playback ended — finished, stalled, or cancelled mid-stream —
    // tell the server now instead of leaving "Now Playing" stuck until the
    // background cleanup sweep gets to it (up to 15 minutes later).
    postVideoDone(jobId);

    transitionTo(AppState::IDLE);
    lastJobPollMs = millis();
}

// Applies a WiFi network the website submitted while we were already
// online. The currently-saved NVS credentials are left untouched until the
// new network proves it works, so a bad password just falls back to what
// was already working (and gets reported back to the website either way).
static void applyPendingWifi(const String& ssid, const String& password) {
    remoteLog("[WiFi] Website requested switch to '%s'", ssid.c_str());
    remoteLogFlush();
    WiFi.begin(ssid.c_str(), password.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(300); yield(); }

    if (WiFi.status() == WL_CONNECTED) {
        wifiStoreSave(ssid, password);
        remoteLog("[WiFi] Switch succeeded");
        postWifiAck("applied");
    } else {
        remoteLog("[WiFi] Switch failed, reverting");
        WiFi.disconnect();
        connectWiFi(20000); // back to the still-saved previous network
        postWifiAck("failed"); // sent over the restored connection
    }
    remoteLogFlush();
}

static void handleDeviceStatePoll() {
    if (WiFi.status() != WL_CONNECTED) return;
    DeviceState state;
    if (!getDeviceState(state)) return;

    // Applied before the screen-specific calls below so an orientation
    // flip and a theme/quote/focus change landing on the same poll both
    // take effect together, rather than the screen redrawing twice.
    bool orientationChanged = displaySetOrientation(state.orientation);

    idleScreenSetTodoSummary(state.pendingCount, state.nextTask);
    idleScreenSetTheme(state.bgTheme);
    idleScreenSetQuote(state.quote);
    setDeviceVolume(state.volume);

    // Focus mode takes priority over the idle screen but doesn't interrupt
    // video playback (handleDeviceStatePoll only ever runs from IDLE/
    // FOCUS_TIMER, never VIDEO_PLAYING) and itself isn't interrupted by a
    // queued video — handleVideoJobIfAny() simply isn't called while in
    // FOCUS_TIMER, so a queued job just waits until the session ends.
    if (state.focusActive) {
        if (g_appState == AppState::IDLE) {
            transitionTo(AppState::FOCUS_TIMER);
            focusTimerEnter(state.focusLabel);
        } else if (orientationChanged) {
            // Still in FOCUS_TIMER — re-run Enter() so cached layout
            // state (and tft.width()/height()-dependent drawing) resets
            // against the new dimensions, same as a fresh entry would.
            focusTimerEnter(state.focusLabel);
        }
        focusTimerSetRemaining(state.focusSecondsRemaining);
    } else if (g_appState == AppState::FOCUS_TIMER) {
        transitionTo(AppState::IDLE); // already calls idleScreenEnter()
    } else if (orientationChanged && g_appState == AppState::IDLE) {
        idleScreenEnter();
    }

    if (state.pendingWifiSsid.length() > 0) {
        applyPendingWifi(state.pendingWifiSsid, state.pendingWifiPassword);
    }
}

void setup() {
    Serial.begin(115200);
    delay(400);

    // First thing logged, before anything else can go wrong — this is the
    // one signal we can't get any other way once the device reboots.
    remoteLog("[Boot] Reset reason: %s", resetReasonStr(esp_reset_reason()));

    displayInit();
    transitionTo(AppState::BOOT);

    initVideoSubsystem();

    // First-boot convenience: seed NVS from config.h so a fresh device
    // still connects without needing the setup portal, if you'd rather
    // just fill in config.h once. Every boot after this reads NVS only —
    // config.h's WIFI_SSID_CFG/WIFI_PASS_CFG are never consulted again.
    String seedSsid, seedPass;
    if (!wifiStoreLoad(seedSsid, seedPass)) {
        wifiStoreSave(WIFI_SSID_CFG, WIFI_PASS_CFG);
    }

    transitionTo(AppState::WIFI_CONNECT);
    if (connectWiFi()) {
        remoteLog("[WiFi] %s", WiFi.localIP().toString().c_str());
        initNTP();
        transitionTo(AppState::IDLE);
        remoteLogFlush(); // get the boot-reason line (and anything else queued) out immediately
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
                wifiFailCount = 0;
                remoteLog("[WiFi] %s", WiFi.localIP().toString().c_str());
                initNTP();
                transitionTo(AppState::IDLE);
                remoteLogFlush();
            } else {
                wifiFailCount++;
                remoteLog("[WiFi] Connect attempt %d/%d failed", wifiFailCount, WIFI_FAIL_PORTAL_THRESHOLD);
                if (wifiFailCount >= WIFI_FAIL_PORTAL_THRESHOLD) {
                    remoteLog("[WiFi] Opening setup portal after repeated failures");
                    remoteLogFlush();
                    transitionTo(AppState::WIFI_PORTAL);
                    wifiPortalRun(); // blocking; restarts the chip on any outcome
                } else {
                    showScreen("WiFi FAILED", "Retrying...");
                    delay(2000);
                }
            }
            break;

        case AppState::WIFI_PORTAL:
            break; // entered+handled synchronously above, never observed here

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
            if (now - lastLogFlushMs >= 5000) {
                lastLogFlushMs = now;
                remoteLogFlush();
            }
            break;
        }

        case AppState::VIDEO_STARTING:
        case AppState::VIDEO_PLAYING:
            // playVideo() is blocking and returns before g_appState would
            // ever be observed here — nothing to do.
            break;

        case AppState::FOCUS_TIMER: {
            focusTimerTick();
            if (focusTimerExpired()) {
                transitionTo(AppState::IDLE);
                break;
            }

            uint32_t now = millis();
            if (now - lastDeviceStateMs >= DEVICE_STATE_POLL_INTERVAL_MS) {
                lastDeviceStateMs = now;
                handleDeviceStatePoll();
            }
            if (now - lastLogFlushMs >= 5000) {
                lastLogFlushMs = now;
                remoteLogFlush();
            }
            break;
        }
    }

    yield();
}
