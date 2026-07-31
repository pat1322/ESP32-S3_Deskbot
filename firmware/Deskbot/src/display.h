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

// Applies "landscape"/"portrait" (unknown values fall back to landscape)
// via tft.setRotation() — a no-op if it matches the current orientation.
// Returns true if it actually changed (callers use this to know whether
// they need to re-run their screen's Enter()/redraw now that
// tft.width()/height() have swapped).
bool displaySetOrientation(const String& orientation);

// Full-screen status/message helpers, used during boot/wifi/video states.
void showScreen(const char* title, const char* sub = "");
void showStatus(const char* msg, uint16_t col = COLOR_CYAN);
void showBufferingBar(int filled, int total);
