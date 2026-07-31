#pragma once

enum class AppState { BOOT, WIFI_CONNECT, WIFI_PORTAL, IDLE, VIDEO_STARTING, VIDEO_PLAYING, FOCUS_TIMER, PHOTO_VIEW };

extern AppState g_appState;

// Runs any on-entry side effects (screen changes, timer resets) and
// updates g_appState.
void transitionTo(AppState newState);
