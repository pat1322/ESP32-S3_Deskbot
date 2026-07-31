#include "focus_timer.h"

#include "display.h"
#include "pins.h"

static const int LABEL_Y   = 54;   // text size 2
static const int CLOCK_Y   = 96;   // text size 6
static const int HINT_Y    = 176;  // text size 1

static String   cachedLabel   = "";
static int      lastServerRemaining = 0;
static uint32_t lastServerAtMs      = 0;
static int      lastDisplayedRemaining = -1;
static uint32_t lastTickMs = 0;

static void drawCountdown(int secondsRemaining) {
    int mm = secondsRemaining / 60;
    int ss = secondsRemaining % 60;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", mm, ss);

    tft.setTextSize(6);
    tft.setTextColor(COLOR_AMBER, TFT_BLACK);
    int w = (int)strlen(buf) * 6 * 6;
    tft.fillRect(0, CLOCK_Y - 4, TFT_W, 48, TFT_BLACK);
    tft.setCursor(max(0, (TFT_W - w) / 2), CLOCK_Y);
    tft.print(buf);
}

void focusTimerEnter(const String& label) {
    tft.fillScreen(TFT_BLACK);

    cachedLabel = label;
    tft.setTextSize(2);
    tft.setTextColor(COLOR_CYAN, TFT_BLACK);
    int w = (int)label.length() * 6 * 2;
    tft.setCursor(max(0, (TFT_W - w) / 2), LABEL_Y);
    tft.print(label);

    const char* hint = "Started from the website";
    tft.setTextSize(1);
    tft.setTextColor(COLOR_MUTED, TFT_BLACK);
    int hw = (int)strlen(hint) * 6;
    tft.setCursor(max(0, (TFT_W - hw) / 2), HINT_Y);
    tft.print(hint);

    lastDisplayedRemaining = -1;
    lastTickMs = 0;
}

void focusTimerSetRemaining(int secondsRemaining) {
    lastServerRemaining = max(0, secondsRemaining);
    lastServerAtMs      = millis();
}

void focusTimerTick() {
    uint32_t now = millis();
    if (now - lastTickMs < 1000 && lastDisplayedRemaining != -1) return;
    lastTickMs = now;

    uint32_t elapsedS = (now - lastServerAtMs) / 1000;
    int remaining = (int)max(0, lastServerRemaining - (int)elapsedS);

    if (remaining != lastDisplayedRemaining) {
        drawCountdown(remaining);
        lastDisplayedRemaining = remaining;
    }
}

bool focusTimerExpired() {
    return lastDisplayedRemaining == 0;
}
