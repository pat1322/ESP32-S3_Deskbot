#include "boot_screen.h"

#include <math.h>

#include "display.h"

// Local duplicate of idle_screen.cpp's alpha-blend helper — this module is
// called before any other screen module has run (setup(), pre-state-machine)
// and deliberately doesn't depend on idle_screen.cpp to stay self-contained.
static uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t alpha) {
    uint8_t fr = (fg >> 11) & 0x1F, fgn = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    uint8_t br = (bg >> 11) & 0x1F, bgn = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    uint8_t r = br + ((int)(fr - br) * alpha) / 255;
    uint8_t g = bgn + ((int)(fgn - bgn) * alpha) / 255;
    uint8_t b = bb + ((int)(fb - bb) * alpha) / 255;
    return (r << 11) | (g << 5) | b;
}

// Matches the website brand mark (index.html's .brand-mark SVG): a dot at
// top (cyan), bottom-right (amber), bottom-left (cyan), each resting just
// outside the ring's circumference. Angles are screen coords (y-down),
// 0 = right, so -90deg is "up".
struct BootDot {
    float angle;
    uint16_t color;
};
static BootDot dots[3] = {
    { -1.5708f, COLOR_CYAN  }, // top
    {  0.5236f, COLOR_AMBER }, // bottom-right
    {  2.6180f, COLOR_CYAN  }, // bottom-left
};

static void drawDot(int cx, int cy, float angle, float radius, int dotR, uint16_t color) {
    int x = cx + (int)(radius * cosf(angle));
    int y = cy + (int)(radius * sinf(angle));
    tft.fillCircle(x, y, dotR, color);
}

// Redraws the same string at increasing brightness (blended from bg toward
// the target color) rather than a true alpha composite — cheap, and reads
// as a fade-in on a TFT with no per-pixel framebuffer access here.
static void fadeInText(const char* text, int textSize, uint16_t color, int cx, int y,
                        uint16_t bg, int steps, int stepDelayMs) {
    int w = (int)strlen(text) * 6 * textSize;
    int x = max(0, cx - w / 2);
    tft.setTextSize(textSize);
    for (int i = 1; i <= steps; i++) {
        uint8_t alpha = (uint8_t)(255 * i / steps);
        tft.setTextColor(blend565(color, bg, alpha), bg);
        tft.setCursor(x, y);
        tft.print(text);
        delay(stepDelayMs);
    }
}

void bootScreenPlay() {
    tft.fillScreen(TFT_BLACK);

    int cx = tft.width() / 2;
    int cy = tft.height() / 2 - 40; // leave room below for the wordmark + credit line
    const int ringRadius   = 34;
    const int orbitRadius  = ringRadius + 6;
    const int dotRadius    = 5;

    // Phase 1 (~700ms): three dots spiral inward from a wide orbit, with
    // some extra spin that dies out as they settle onto their final
    // resting angles on the ring.
    const int   convergeSteps  = 24;
    const int   convergeStepMs = 28;
    const float startRadius    = orbitRadius + 70.0f;
    int eraseX = max(0, cx - (int)startRadius - dotRadius);
    int eraseY = max(0, cy - (int)startRadius - dotRadius);
    int eraseW = min(tft.width()  - eraseX, ((int)startRadius + dotRadius) * 2);
    int eraseH = min(tft.height() - eraseY, ((int)startRadius + dotRadius) * 2);

    for (int s = 1; s <= convergeSteps; s++) {
        float t = (float)s / convergeSteps;
        float eased  = 1.0f - (1.0f - t) * (1.0f - t); // ease-out
        float radius = startRadius + (orbitRadius - startRadius) * eased;
        float spin   = (1.0f - eased) * 3.0f;

        tft.fillRect(eraseX, eraseY, eraseW, eraseH, TFT_BLACK);
        for (auto& d : dots) drawDot(cx, cy, d.angle + spin, radius, dotRadius, d.color);
        delay(convergeStepMs);
    }

    // Phase 2 (~350ms): the ring draws itself outward from the dots'
    // resting orbit to full size, dots held in their final position on top.
    const int ringSteps = 12;
    for (int s = 1; s <= ringSteps; s++) {
        float t = (float)s / ringSteps;
        tft.drawCircle(cx, cy, (int)(ringRadius * t), COLOR_AMBER);
        for (auto& d : dots) drawDot(cx, cy, d.angle, orbitRadius, dotRadius, d.color);
        delay(28);
    }
    tft.drawCircle(cx, cy, ringRadius, COLOR_AMBER);
    tft.drawCircle(cx, cy, ringRadius - 1, COLOR_AMBER); // slightly thicker stroke

    // Phase 3: wordmark, then the smaller credit line, each fading in.
    fadeInText("DESKBOT", 3, COLOR_AMBER, cx, cy + ringRadius + 22, TFT_BLACK, 8, 30);
    fadeInText("Developed by Patrick Perez", 1, COLOR_MUTED, cx, cy + ringRadius + 52, TFT_BLACK, 8, 20);

    delay(500); // brief hold so the finished mark is actually readable
}
