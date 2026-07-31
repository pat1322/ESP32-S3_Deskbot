#include "idle_screen.h"

#include <math.h>
#include <time.h>
#include <WiFi.h>

#include "display.h"
#include "network.h"
#include "pins.h"

// ── Layout ──────────────────────────────────────────────────────────
// Y-positions/heights are landscape-tuned constants, kept as-is in
// portrait too (leaves extra blank space below TODO_Y on the taller
// portrait screen rather than a full proportional relayout — accepted
// v1 trade-off). BAND_W and WIFI_X are screen-width-relative, so they
// track tft.width() live instead of being fixed constants.
static const int BAND_X = 0,   BAND_Y = 6,   BAND_H = 50;
static const int CLOCK_Y = 72;   // text size 4
static const int DATE_Y  = 114;  // text size 2
static const int QUOTE_Y = 155;  // text size 1, in the blank gap below the date
static const int DIVIDER_Y = 196;
static const int TODO_Y  = 210;  // text size 1
static const int WIFI_Y = 8; // top-right signal bars

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

static String   cachedQuote = "";
static bool     quoteDirty = true;

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

static void renderStarfield() {
    for (auto& s : stars) {
        s.x += s.speed;
        if (s.x > band.width()) s.x = 0;
        band.fillCircle((int)s.x, (int)s.y, 1, s.color);
    }
}

static void renderAmbientBand() {
    if (currentTheme == BgTheme::MINIMAL) return; // band stays blank, nothing to push

    band.fillSprite(TFT_BLACK);
    float t = millis() / 1000.0f;
    switch (currentTheme) {
        case BgTheme::DRIFT:     renderDrift(t);   break;
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

    tft.fillRect(0, TODO_Y - 2, tft.width(), 14, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_MUTED, TFT_BLACK);
    tft.setCursor(max(0, (tft.width() - (int)line.length() * 6) / 2), TODO_Y);
    tft.print(line);
    todoDirty = false;
}

static void renderQuoteLine() {
    // Wipe the whole gap regardless of length so a shorter new quote
    // doesn't leave stray characters from a longer previous one.
    tft.fillRect(0, QUOTE_Y - 2, tft.width(), 14, TFT_BLACK);
    if (cachedQuote.length() > 0) {
        String line = cachedQuote;
        if (line.length() > 48) line = line.substring(0, 45) + "...";
        tft.setTextSize(1);
        tft.setTextColor(COLOR_MUTED, TFT_BLACK);
        tft.setCursor(max(0, (tft.width() - (int)line.length() * 6) / 2), QUOTE_Y);
        tft.print(line);
    }
    quoteDirty = false;
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

    int wifiX = tft.width() - 26;
    tft.fillRect(wifiX - 2, WIFI_Y - 2, 22, 14, TFT_BLACK);
    for (int i = 0; i < 3; i++) {
        int barH = 4 + i * 4;
        int x = wifiX + i * 6;
        int y = WIFI_Y + 12 - barH;
        uint16_t col = (i < bars) ? COLOR_CYAN : TFT_DARKGREY;
        tft.fillRect(x, y, 4, barH, col);
    }
}

void idleScreenEnter() {
    tft.fillScreen(TFT_BLACK);
    tft.drawFastHLine(0, DIVIDER_Y, tft.width(), COLOR_MUTED);

    lastClockStr = "\x01";
    lastDateStr  = "\x01";
    lastWifiBars = -2;
    todoDirty    = true;
    quoteDirty   = true;
    lastClockMs  = 0;
    lastAnimMs   = 0;

    // Recreate the band sprite if it's never existed, or if the screen
    // width changed since it was created (an orientation switch) — a
    // TFT_eSprite doesn't resize itself, so a stale-width sprite would
    // either waste space or (worse) clip the ambient animation.
    if (band.width() == 0 || band.width() != tft.width()) {
        if (band.width() != 0) band.deleteSprite();
        band.setColorDepth(16);
        band.createSprite(tft.width(), BAND_H);
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
            tft.fillRect(0, CLOCK_Y - 2, tft.width(), 34, TFT_BLACK);
            tft.setCursor(max(0, (tft.width() - w) / 2), CLOCK_Y);
            tft.print(clockStr);
            lastClockStr = clockStr;
        }

        String dateStr(dateBuf);
        if (dateStr != lastDateStr) {
            tft.setTextSize(2);
            tft.setTextColor(COLOR_MUTED, TFT_BLACK);
            int w = dateStr.length() * 6 * 2;
            tft.fillRect(0, DATE_Y - 2, tft.width(), 18, TFT_BLACK);
            tft.setCursor(max(0, (tft.width() - w) / 2), DATE_Y);
            tft.print(dateStr);
            lastDateStr = dateStr;
        }

        drawWifiIndicator();
    }

    if (todoDirty) renderTodoLine();
    if (quoteDirty) renderQuoteLine();
}

void idleScreenSetTodoSummary(int pendingCount, const String& nextTask) {
    if (pendingCount != cachedPending || nextTask != cachedNextTask) {
        cachedPending  = pendingCount;
        cachedNextTask = nextTask;
        todoDirty      = true;
    }
}

void idleScreenSetQuote(const String& quote) {
    if (quote != cachedQuote) {
        cachedQuote = quote;
        quoteDirty  = true;
    }
}

void idleScreenSetTheme(const String& themeName) {
    BgTheme next = BgTheme::DRIFT;
    if      (themeName == "starfield") next = BgTheme::STARFIELD;
    else if (themeName == "minimal")   next = BgTheme::MINIMAL;

    if (next == currentTheme) return;
    currentTheme = next;

    if (band.width() > 0) {
        band.fillSprite(TFT_BLACK);
        band.pushSprite(BAND_X, BAND_Y); // clear leftover frame from the old theme
    }
}