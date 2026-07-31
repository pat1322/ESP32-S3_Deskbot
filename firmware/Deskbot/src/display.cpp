#include "display.h"

TFT_eSPI tft = TFT_eSPI();

// setRotation(1) is today's (only, until now) landscape. Portrait is wired
// to setRotation(0) as a starting guess — this is the one thing in this
// feature that can't be confirmed without the physical panel; flip to
// setRotation(2) here if 0 comes out upside-down or mirrored.
static String currentOrientation = "landscape";

void displayInit() {
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setSwapBytes(true);
}

bool displaySetOrientation(const String& orientation) {
    String next = (orientation == "portrait") ? "portrait" : "landscape";
    if (next == currentOrientation) return false;
    currentOrientation = next;
    tft.setRotation(next == "portrait" ? 0 : 1);
    tft.fillScreen(TFT_BLACK);
    return true;
}

void showScreen(const char* title, const char* sub) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextWrap(false);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(max(0, (tft.width() - (int)strlen(title) * 12) / 2), tft.height() / 2 - 22);
    tft.print(title);
    if (strlen(sub) > 0) {
        tft.setTextSize(1);
        tft.setTextColor(COLOR_CYAN, TFT_BLACK);
        tft.setCursor(max(0, (tft.width() - (int)strlen(sub) * 6) / 2), tft.height() / 2 + 10);
        tft.print(sub);
    }
}

void showStatus(const char* msg, uint16_t col) {
    tft.fillRect(0, tft.height() - 18, tft.width(), 18, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(col, TFT_BLACK);
    tft.setCursor(4, tft.height() - 12);
    tft.print(msg);
}

void showBufferingBar(int filled, int total) {
    const int BX = 20, BY = tft.height() / 2 + 28, BW = tft.width() - 40, BH = 6;
    tft.drawRect(BX - 1, BY - 1, BW + 2, BH + 2, TFT_DARKGREY);
    int fw = (int)((float)filled / total * BW);
    if (fw > 0) tft.fillRect(BX, BY, min(fw, BW), BH, COLOR_AMBER);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", (int)((float)filled / total * 100));
    tft.setTextSize(1);
    tft.setTextColor(COLOR_AMBER, TFT_BLACK);
    tft.setCursor((tft.width() - (int)strlen(pct) * 6) / 2, BY + 12);
    tft.print(pct);
}
