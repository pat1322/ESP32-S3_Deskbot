#include "photo_view.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>

#include "display.h"
#include "pins.h"
#include "remote_log.h"
#include "../config.h"

static JPEGDEC jpeg;
static uint8_t* photoBuf = nullptr;

// Global, not local to photoViewEnter() — same fragmentation reasoning
// already applied to video_player.cpp's audio globals: this is reused
// every time a photo is shown, not just once, so a stack-local
// WiFiClientSecure here would churn its TLS session buffers repeatedly
// over a long-running session.
static WiFiClientSecure photoCli;

static String serverBase() {
    String base = String(DESKBOT_SERVER_URL);
    while (base.endsWith("/")) base.remove(base.length() - 1);
    return base;
}

static int jpegDrawCallback(JPEGDRAW* pDraw) {
    tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
    return 1;
}

void photoViewEnter(const String& photoId) {
    tft.fillScreen(TFT_BLACK);

    if (!photoBuf) {
        photoBuf = (uint8_t*)malloc(PHOTO_BUF_SIZE);
        if (!photoBuf) {
            remoteLog("[Photo] buffer alloc failed");
            remoteLogFlush();
            showScreen("Photo Error", "Out of memory");
            return;
        }
    }

    photoCli.setInsecure();
    photoCli.setConnectionTimeout(8000);
    HTTPClient http;
    http.begin(photoCli, serverBase() + "/photo/stream/" + photoId + ".jpg");
    http.addHeader("X-Api-Key", DESKBOT_API_KEY);
    http.setTimeout(8000);

    int code = http.GET();
    remoteLog("[Photo] HTTP %d", code);
    if (code != 200) {
        remoteLogFlush();
        http.end();
        showScreen("Photo Error", ("HTTP " + String(code)).c_str());
        return;
    }

    // FileResponse (routers/photo.py) always sets Content-Length, so this
    // is trustworthy — same reasoning video.py's streams needed explicit
    // Content-Length for after the chunked-encoding fix.
    int total = http.getSize();
    if (total <= 0 || total > PHOTO_BUF_SIZE) {
        remoteLog("[Photo] unexpected size %d", total);
        remoteLogFlush();
        http.end();
        showScreen("Photo Error", "Unexpected size");
        return;
    }

    WiFiClient* s = http.getStreamPtr();
    int got = 0;
    uint32_t deadline = millis() + 10000;
    while (got < total && millis() < deadline) {
        int avail = s->available();
        if (avail > 0) {
            got += s->readBytes(photoBuf + got, min(avail, total - got));
        } else {
            yield();
        }
    }
    http.end();

    if (got < total) {
        remoteLog("[Photo] incomplete download: %d/%d bytes", got, total);
        remoteLogFlush();
        showScreen("Photo Error", "Incomplete download");
        return;
    }

    tft.fillScreen(TFT_BLACK);
    if (jpeg.openRAM(photoBuf, got, jpegDrawCallback)) {
        // Same center-crop math as video_player.cpp's frame draw — a
        // negative origin here is intentional (see CLAUDE.md).
        jpeg.decode(
            (tft.width()  - jpeg.getWidth())  / 2,
            (tft.height() - jpeg.getHeight()) / 2,
            0
        );
        jpeg.close();
        remoteLog("[Photo] displayed (%d bytes)", got);
    } else {
        remoteLog("[Photo] JPEG decode failed");
        showScreen("Photo Error", "Decode failed");
    }
    remoteLogFlush();
}

void photoViewTick() {
    // Static display — nothing to do per-tick. Exists for loop()
    // consistency with idleScreenTick()/focusTimerTick().
}
