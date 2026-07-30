#pragma once

enum class AppState { BOOT, WIFI_CONNECT, IDLE, VIDEO_STARTING, VIDEO_PLAYING };

extern AppState g_appState;

// Runs any on-entry side effects (screen changes, timer resets) and
// updates g_appState.
void transitionTo(AppState newState);
