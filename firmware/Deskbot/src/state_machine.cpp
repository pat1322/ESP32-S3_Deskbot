#include "state_machine.h"

#include "display.h"
#include "idle_screen.h"

AppState g_appState = AppState::BOOT;

void transitionTo(AppState newState) {
    g_appState = newState;

    switch (newState) {
        case AppState::BOOT:
            showScreen("Booting...");
            break;
        case AppState::WIFI_CONNECT:
            showScreen("Connecting...", "Joining WiFi");
            break;
        case AppState::WIFI_PORTAL:
            showScreen("Setup Mode", "Join 'Deskbot-Setup' WiFi");
            break;
        case AppState::IDLE:
            idleScreenEnter();
            break;
        case AppState::VIDEO_STARTING:
            // caller (Deskbot.ino) shows the "Starting..." screen with the
            // job title before invoking playVideo(), which owns the screen
            // from that point on.
            break;
        case AppState::VIDEO_PLAYING:
            break;
        case AppState::FOCUS_TIMER:
            // caller (Deskbot.ino) calls focusTimerEnter(label) itself,
            // same reasoning as VIDEO_STARTING above — it has the label
            // in hand from the poll response, this switch doesn't.
            break;
    }
}
