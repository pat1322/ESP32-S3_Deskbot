#include "idle_screen.h"

#include <math.h>
#include <time.h>
#include <WiFi.h>

#include "display.h"
#include "network.h"
#include "pins.h"

// ── Layout ──────────────────────────────────────────────────────────
static const int BAND_X = 0,   BAND_Y = 6,   BAND_W = TFT_W, BAND_H = 50;
static const int CLOCK_Y = 72;   // text size 4
static const int DATE_Y  = 114;  // text size 2
static const int DIVIDER_Y = 196;
static const int TODO_Y  = 210;  // text size 1
static const int WIFI_X = TFT_W - 26, WIFI_Y = 8; // top-right signal bars

static TFT_eSprite band(&tft);

static BgTheme currentTheme = BgTheme::DRIFT;

// ── Drift theme state ───────────────────────────────────────────────
struct Orb {
    float baseX, baseY, ampX, ampY, speed, phase;
    int   radius;
    uint16_t color;
};

static Orb orbs[3] = {
    { 70,  24, 22, 14, 0.55f, 0.0f, 9, COLOR_AMBER },
    { 230, 20, 18, 16, 0.42f, 2.1f, 8, COLOR_CYAN  },
    { 150, 30, 26, 10, 0.35f, 4.4f, 6, COLOR_AMBER },
};

// ── Pulse theme state ───────────────────────────────────────────────
struct Ring {
    float phase; // 0..1, wraps
    float speed; // phase units per animation tick
    uint16_t color;
};

static Ring rings[3] = {
    { 0.0f,  0.012f, COLOR_AMBER },
    { 0.33f, 0.012f, COLOR_CYAN  },
    { 0.66f, 0.012f, COLOR_AMBER },
};

// ── Starfield theme state ───────────────────────────────────────────
struct Star {
    float x, y, speed;
    uint16_t color;
};

static Star stars[6] = {
    { 0,   10, 1.6f, COLOR_AMBER },
    { 60,  22, 1.1f, COLOR_CYAN  },
    { 120, 34, 1.9f, COLOR_AMBER },
    { 180, 16, 1.3f, COLOR_CYAN  },
    { 240, 40, 1.5f, COLOR_AMBER },
    { 300, 28, 1.0f, COLOR_CYAN  },
};

static uint32_t lastClockMs = 0;
static uint32_t lastAnimMs  = 0;
static String   lastClockStr = "\x01"; // sentinel so the first tick always redraws
static String   lastDateStr  = "\x01";
static int      lastWifiBars = -2;

static int      cachedPending = -1;
static String   cachedNextTask = "";
static bool     todoDirty = true;

static uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t alpha) {
    uint8_t fr = (fg >> 11) & 0x1F, fgn = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    uint8_t br = (bg >> 11) & 0x1F, bgn = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    uint8_t r = br + ((int)(fr - br) * alpha) / 255;
    uint8_t g = bgn + ((int)(fgn - bgn) * alpha) / 255;
    uint8_t b = bb + ((int)(fb - bb) * alpha) / 255;
    return (r << 11) | (g << 5) | b;
}

static void drawOrb(const Orb& o, float t) {
    int cx = (int)(o.baseX + o.ampX * sinf(t * o.speed + o.phase));
    int cy = (int)(o.baseY + o.ampY * cosf(t * o.speed * 0.8f + o.phase));
    for (int r = o.radius; r >= 1; r--) {
        uint8_t alpha = map(r, o.radius, 1, 55, 200);
        band.fillCircle(cx, cy, r, blend565(o.color, TFT_BLACK, alpha));
    }
}

static void renderDrift(float t) {
    for (auto& o : orbs) drawOrb(o, t);
}

static void renderPulse() {
    const int cx = BAND_W / 2, cy = BAND_H / 2, maxR = BAND_H / 2 - 1;
    for (auto& ring : rings) {
        ring.phase += ring.speed;
        if (ring.phase > 1.0f) ring.phase -= 1.0f;
        int radius = (int)(ring.phase * maxR);
        uint8_t alpha = (uint8_t)(200 * (1.0f - ring.phase));
        if (radius >= 1) band.drawCircle(cx, cy, radius, blend565(ring.color, TFT_BLACK, alpha));
    }
}

static void renderStarfield() {
    for (auto& s : stars) {
        s.x += s.speed;
        if (s.x > BAND_W) s.x = 0;
        band.fillCircle((int)s.x, (int)s.y, 1, s.color);
    }
}

static void renderAmbientBand() {
    if (currentTheme == BgTheme::MINIMAL) return; // band stays blank, nothing to push

    band.fillSprite(TFT_BLACK);
    float t = millis() / 1000.0f;
    switch (currentTheme) {
        case BgTheme::DRIFT:     renderDrift(t);   break;
        case BgTheme::PULSE:     renderPulse();    break;
        case BgTheme::STARFIELD: renderStarfield(); break;
        default: break;
    }
    band.pushSprite(BAND_X, BAND_Y);
}

static void renderTodoLine() {
    String line = (cachedPending <= 0)
        ? "No tasks pending"
        : String(cachedPending) + (cachedPending == 1 ? " task: " : " tasks — next: ") + cachedNextTask;
    if (line.length() > 42) line = line.substring(0, 39) + "...";

    tft.fillRect(0, TODO_Y - 2, TFT_W, 14, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_MUTED, TFT_BLACK);
    tft.setCursor(max(0, (TFT_W - (int)line.length() * 6) / 2), TODO_Y);
    tft.print(line);
    todoDirty = false;
}

static int wifiBars() {
    if (WiFi.status() != WL_CONNECTED) return 0;
    int32_t rssi = WiFi.RSSI();
    if (rssi > -60) return 3;
    if (rssi > -75) return 2;
    return 1;
}

static void drawWifiIndicator() {
    int bars = wifiBars();
    if (bars == lastWifiBars) return;
    lastWifiBars = bars;

    tft.fillRect(WIFI_X - 2, WIFI_Y - 2, 22, 14, TFT_BLACK);
    for (int i = 0; i < 3; i++) {
        int barH = 4 + i * 4;
        int x = WIFI_X + i * 6;
        int y = WIFI_Y + 12 - barH;
        uint16_t col = (i < bars) ? COLOR_CYAN : TFT_DARKGREY;
        tft.fillRect(x, y, 4, barH, col);
    }
}

void idleScreenEnter() {
    tft.fillScreen(TFT_BLACK);
    tft.drawFastHLine(0, DIVIDER_Y, TFT_W, COLOR_MUTED);

    lastClockStr = "\x01";
    lastDateStr  = "\x01";
    lastWifiBars = -2;
    todoDirty    = true;
    lastClockMs  = 0;
    lastAnimMs   = 0;

    if (band.width() == 0) {
        band.setColorDepth(16);
        band.createSprite(BAND_W, BAND_H);
    }
}

void idleScreenTick() {
    uint32_t now = millis();

    if (now - lastAnimMs >= ANIM_TICK_MS) {
        lastAnimMs = now;
        renderAmbientBand();
    }

    if (now - lastClockMs >= CLOCK_TICK_MS) {
        lastClockMs = now;

        struct tm ti;
        char clockBuf[16] = "--:--:--";
        char dateBuf[24] = "Waiting for time sync";
        if (getLocalTimeSafe(ti)) {
            int h12 = ti.tm_hour % 12;
            if (h12 == 0) h12 = 12;
            const char* ampm = (ti.tm_hour < 12) ? "AM" : "PM";
            snprintf(clockBuf, sizeof(clockBuf), "%d:%02d:%02d %s", h12, ti.tm_min, ti.tm_sec, ampm);
            strftime(dateBuf, sizeof(dateBuf), "%a, %b %d", &ti);
        }

        String clockStr(clockBuf);
        if (clockStr != lastClockStr) {
            tft.setTextSize(4);
            tft.setTextColor(COLOR_AMBER, TFT_BLACK);
            int w = clockStr.length() * 6 * 4;
            tft.fillRect(0, CLOCK_Y - 2, TFT_W, 34, TFT_BLACK);
            tft.setCursor(max(0, (TFT_W - w) / 2), CLOCK_Y);
            tft.print(clockStr);
            lastClockStr = clockStr;
        }

        String dateStr(dateBuf);
        if (dateStr != lastDateStr) {
            tft.setTextSize(2);
            tft.setTextColor(COLOR_MUTED, TFT_BLACK);
            int w = dateStr.length() * 6 * 2;
            tft.fillRect(0, DATE_Y - 2, TFT_W, 18, TFT_BLACK);
            tft.setCursor(max(0, (TFT_W - w) / 2), DATE_Y);
            tft.print(dateStr);
            lastDateStr = dateStr;
        }

        drawWifiIndicator();
    }

    if (todoDirty) renderTodoLine();
}

void idleScreenSetTodoSummary(int pendingCount, const String& nextTask) {
    if (pendingCount != cachedPending || nextTask != cachedNextTask) {
        cachedPending  = pendingCount;
        cachedNextTask = nextTask;
        todoDirty      = true;
    }
}

void idleScreenSetTheme(const String& themeName) {
    BgTheme next = BgTheme::DRIFT;
    if      (themeName == "pulse")     next = BgTheme::PULSE;
    else if (themeName == "starfield") next = BgTheme::STARFIELD;
    else if (themeName == "minimal")   next = BgTheme::MINIMAL;

    if (next == currentTheme) return;
    currentTheme = next;

    if (band.width() > 0) {
        band.fillSprite(TFT_BLACK);
        band.pushSprite(BAND_X, BAND_Y); // clear leftover frame from the old theme
    }
}