#include "display.h"

TFT_eSPI tft = TFT_eSPI();

void displayInit() {
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setSwapBytes(true);
}

void showScreen(const char* title, const char* sub) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextWrap(false);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(max(0, (TFT_W - (int)strlen(title) * 12) / 2), TFT_H / 2 - 22);
    tft.print(title);
    if (strlen(sub) > 0) {
        tft.setTextSize(1);
        tft.setTextColor(COLOR_CYAN, TFT_BLACK);
        tft.setCursor(max(0, (TFT_W - (int)strlen(sub) * 6) / 2), TFT_H / 2 + 10);
        tft.print(sub);
    }
}

void showStatus(const char* msg, uint16_t col) {
    tft.fillRect(0, TFT_H - 18, TFT_W, 18, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(col, TFT_BLACK);
    tft.setCursor(4, TFT_H - 12);
    tft.print(msg);
}

void showBufferingBar(int filled, int total) {
    const int BX = 20, BY = TFT_H / 2 + 28, BW = TFT_W - 40, BH = 6;
    tft.drawRect(BX - 1, BY - 1, BW + 2, BH + 2, TFT_DARKGREY);
    int fw = (int)((float)filled / total * BW);
    if (fw > 0) tft.fillRect(BX, BY, min(fw, BW), BH, COLOR_AMBER);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", (int)((float)filled / total * 100));
    tft.setTextSize(1);
    tft.setTextColor(COLOR_AMBER, TFT_BLACK);
    tft.setCursor((TFT_W - (int)strlen(pct) * 6) / 2, BY + 12);
    tft.print(pct);
}
