#pragma once

#include <TFT_eSPI.h>
#include "pins.h"

extern TFT_eSPI tft;

// Palette shared with the website's "Desk Unit" widget (see
// server/app/static/css/style.css) so the physical screen and the web
// preview read as the same product.
#define COLOR_AMBER 0xFCE8
#define COLOR_CYAN  0x5F1B
#define COLOR_MUTED 0x7C32

void displayInit();

// Full-screen status/message helpers, used during boot/wifi/video states.
void showScreen(const char* title, const char* sub = "");
void showStatus(const char* msg, uint16_t col = COLOR_CYAN);
void showBufferingBar(int filled, int total);
