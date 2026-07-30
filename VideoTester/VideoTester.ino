#include "voice_config.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include <JPEGDEC.h>

#include "AudioTools.h"
#include "AudioTools/AudioLibs/I2SCodecStream.h"
#if __has_include("AudioTools/AudioCodecs/CodecMP3Helix.h")
  #include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#elif __has_include("AudioCodecs/CodecMP3Helix.h")
  #include "AudioCodecs/CodecMP3Helix.h"
#else
  #error "CodecMP3Helix not found"
#endif

// ── Tuning ────────────────────────────────────────────────────────────────────
#define TARGET_FPS        20
#define VOLUME            0.4f
#define MJPEG_BUF_SIZE    (1024 * 256)
#define PRE_FILL_BYTES    (1024 * 128)
#define STREAM_STALL_MS   15000
#define AUDIO_PRIME_MS    350
#define POLL_INTERVAL_MS  5000

// ── Pins ──────────────────────────────────────────────────────────────────────
#define PIN_SDA   1
#define PIN_SCL   2
#define PIN_MCLK  38
#define PIN_BCLK  14
#define PIN_WS    13
#define PIN_DOUT  45
#define PIN_DIN   12
#define PIN_PA    48
#define ES_ADDR   0x18

// ── Hardware ──────────────────────────────────────────────────────────────────
TFT_eSPI       tft = TFT_eSPI();
JPEGDEC        jpeg;
DriverPins     brdPins;
AudioBoard     brdDrv(AudioDriverES8311, brdPins);
I2SCodecStream i2sCodec(brdDrv);

#define TFT_W 320
#define TFT_H 240

static uint8_t* mjpegBuf = nullptr;

volatile bool   g_playing       = false;
volatile bool   g_audioReady    = false;
volatile bool   g_startPlayback = false;
static   String g_jobId         = "";
static   String g_serverBase    = "";

// ─────────────────────────────────────────────────────────────────────────────
//  JPEG draw callback
// ─────────────────────────────────────────────────────────────────────────────
int jpegDrawCallback(JPEGDRAW* pDraw) {
    tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Display helpers
// ─────────────────────────────────────────────────────────────────────────────
void showScreen(const char* title, const char* sub = "") {
    tft.fillScreen(TFT_BLACK);
    tft.setTextWrap(false);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(max(0, (TFT_W - (int)strlen(title) * 12) / 2), TFT_H / 2 - 22);
    tft.print(title);
    if (strlen(sub) > 0) {
        tft.setTextSize(1);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(max(0, (TFT_W - (int)strlen(sub) * 6) / 2), TFT_H / 2 + 10);
        tft.print(sub);
    }
}

void showStatus(const char* msg, uint16_t col = TFT_CYAN) {
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
    if (fw > 0) tft.fillRect(BX, BY, min(fw, BW), BH, TFT_CYAN);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", (int)((float)filled / total * 100));
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor((TFT_W - (int)strlen(pct) * 6) / 2, BY + 12);
    tft.print(pct);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Audio task — Core 0
// ─────────────────────────────────────────────────────────────────────────────
void audioTaskFn(void*) {
    MP3DecoderHelix    mp3Dec;
    EncodedAudioStream decoded(&i2sCodec, &mp3Dec);
    mp3Dec.begin();
    decoded.begin();

    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setConnectionTimeout(15000);

    HTTPClient http;
    http.begin(cli, g_serverBase + "/video/stream/" + g_jobId + ".mp3");
    http.addHeader("X-Api-Key", AETHER_API_KEY);
    http.setTimeout(600000);

    int code = http.GET();
    Serial.printf("[Audio] HTTP %d\n", code);

    if (code == 200) {
        WiFiClient* s = http.getStreamPtr();
        g_audioReady = true;
        while (!g_startPlayback && g_playing) { vTaskDelay(1); }

        uint8_t buf[2048];
        while (g_playing) {
            int avail = s->available();
            if (avail > 0) {
                int got = s->readBytes(buf, min(avail, (int)sizeof(buf)));
                if (got > 0) decoded.write(buf, got);
            } else {
                if (!http.connected() && s->available() == 0) break;
                vTaskDelay(1);
            }
        }
    } else {
        g_audioReady = true;
    }

    decoded.end();
    http.end();
    g_playing = false;
    Serial.println("[Audio] Done");
    vTaskDelete(NULL);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Server helpers
// ─────────────────────────────────────────────────────────────────────────────
String checkCurrentJob() {
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setConnectionTimeout(8000);
    HTTPClient http;
    http.begin(cli, g_serverBase + "/video/current");
    http.addHeader("X-Api-Key", AETHER_API_KEY);
    http.setTimeout(8000);
    if (http.GET() != 200) { http.end(); return ""; }
    String resp = http.getString();
    http.end();
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return "";
    if (!(doc["ready"] | false)) return "";
    return doc["job_id"] | "";
}

String getJobTitle(const String& jobId) {
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setConnectionTimeout(8000);
    HTTPClient http;
    http.begin(cli, g_serverBase + "/video/status/" + jobId);
    http.addHeader("X-Api-Key", AETHER_API_KEY);
    http.setTimeout(8000);
    if (http.GET() != 200) { http.end(); return ""; }
    String resp = http.getString();
    http.end();
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return "";
    return doc["title"] | "";
}

void clearCurrentJob() {
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setConnectionTimeout(8000);
    HTTPClient http;
    http.begin(cli, g_serverBase + "/video/current/clear");
    http.addHeader("X-Api-Key", AETHER_API_KEY);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(8000);
    http.POST("{}");
    http.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  playVideo
// ─────────────────────────────────────────────────────────────────────────────
bool playVideo(const String& jobId) {
    g_jobId         = jobId;
    g_playing       = true;
    g_audioReady    = false;
    g_startPlayback = false;

    showScreen("Buffering...", "Opening stream");

    WiFiClientSecure vcli;
    vcli.setInsecure();
    vcli.setConnectionTimeout(20000);

    HTTPClient vhttp;
    vhttp.begin(vcli, g_serverBase + "/video/stream/" + jobId + ".mjpeg");
    vhttp.addHeader("X-Api-Key", AETHER_API_KEY);
    vhttp.setTimeout(600000);

    int code = vhttp.GET();
    Serial.printf("[Video] HTTP %d\n", code);

    if (code != 200) {
        showScreen("Stream Error", ("HTTP " + String(code)).c_str());
        g_playing = false;
        vhttp.end();
        return false;
    }

    WiFiClient* vs = vhttp.getStreamPtr();

    uint8_t streamFps = TARGET_FPS;
    vs->readBytes(&streamFps, 1);
    if (streamFps == 0 || streamFps > 60) streamFps = TARGET_FPS;
    const uint32_t frameMs = 1000 / streamFps;
    Serial.printf("[Video] %u fps  frameMs=%u ms\n", streamFps, frameMs);

    int bytesInBuf = 0;
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(max(0, (TFT_W - 12 * 12) / 2), TFT_H / 2 - 22);
    tft.print("Buffering...");

    int lastBarPct   = -1;
    uint32_t prefillDeadline = millis() + 8000;
    while (bytesInBuf < PRE_FILL_BYTES && millis() < prefillDeadline) {
        int avail = vs->available();
        if (avail > 0) {
            int toRead = min(avail, MJPEG_BUF_SIZE - bytesInBuf);
            if (toRead > 0) bytesInBuf += vs->readBytes(mjpegBuf + bytesInBuf, toRead);
        }
        int pct = (int)((float)bytesInBuf / PRE_FILL_BYTES * 100);
        if (pct != lastBarPct) { showBufferingBar(bytesInBuf, PRE_FILL_BYTES); lastBarPct = pct; }
        yield();
    }
    Serial.printf("[Video] Pre-filled %d bytes\n", bytesInBuf);

    showScreen("Buffering...", "Starting audio...");
    xTaskCreatePinnedToCore(audioTaskFn, "vid_audio", 32768, NULL, 2, NULL, 0);
    uint32_t t0 = millis();
    while (!g_audioReady && millis() - t0 < 8000) { delay(50); yield(); }

    tft.fillScreen(TFT_BLACK);
    showStatus("Playing", TFT_GREEN);
    g_startPlayback = true;

    delay(AUDIO_PRIME_MS);
    uint32_t playStartMs = millis();
    uint32_t frameCount  = 0;
    uint32_t lastByteMs  = millis();
    uint32_t fpsFrames   = 0;
    uint32_t fpsWindowMs = millis();

    while (g_playing) {

        int avail = vs->available();
        if (avail > 0) {
            int toRead = min(avail, MJPEG_BUF_SIZE - bytesInBuf);
            if (toRead > 0) {
                int got = vs->readBytes(mjpegBuf + bytesInBuf, toRead);
                bytesInBuf += got;
                if (got > 0) lastByteMs = millis();
            }
        }

        int frameStart = -1, frameEnd = -1;
        for (int i = 0; i < bytesInBuf - 1; i++) {
            if (mjpegBuf[i] != 0xFF) continue;
            if      (mjpegBuf[i+1] == 0xD8) { frameStart = i; }
            else if (mjpegBuf[i+1] == 0xD9 && frameStart != -1) { frameEnd = i + 1; break; }
        }

        if (frameStart != -1 && frameEnd != -1) {
            frameCount++;
            fpsFrames++;

            uint32_t elapsedMs  = millis() - playStartMs;
            uint32_t expectedMs = frameCount * frameMs;

            if (elapsedMs <= expectedMs + frameMs * 10) {
                while (millis() - playStartMs < expectedMs) yield();

                int frameSize = frameEnd - frameStart + 1;
                if (jpeg.openRAM(mjpegBuf + frameStart, frameSize, jpegDrawCallback)) {
                    jpeg.decode(
                        max(0, (TFT_W - jpeg.getWidth())  / 2),
                        max(0, (TFT_H - jpeg.getHeight()) / 2),
                        0
                    );
                    jpeg.close();
                }
            }

            int remaining = bytesInBuf - frameEnd - 1;
            if (remaining > 0) memmove(mjpegBuf, mjpegBuf + frameEnd + 1, remaining);
            bytesInBuf = max(0, remaining);

            if (millis() - fpsWindowMs >= 5000) {
                Serial.printf("[Video] %.1f fps\n", fpsFrames / 5.0f);
                fpsFrames   = 0;
                fpsWindowMs = millis();
            }

        } else {
            if ((!vhttp.connected() && vs->available() == 0) ||
                 (millis() - lastByteMs) > STREAM_STALL_MS) {
                Serial.println("[Video] Stream ended");
                break;
            }
            yield();
        }
    }

    g_playing = false;
    vhttp.end();
    Serial.printf("[Video] Done — %u frames\n", frameCount);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  setup
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
    delay(400);

    pinMode(PIN_PA, OUTPUT); digitalWrite(PIN_PA, LOW);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setSwapBytes(true);
    showScreen("Booting...");

    if (psramFound()) {
        mjpegBuf = (uint8_t*)ps_malloc(MJPEG_BUF_SIZE);
        Serial.printf("[Init] PSRAM: %d KB\n", MJPEG_BUF_SIZE / 1024);
    } else {
        mjpegBuf = (uint8_t*)malloc(MJPEG_BUF_SIZE);
        Serial.println("[Init] No PSRAM — heap");
    }
    if (!mjpegBuf) { showScreen("MEM ERROR"); while (1) delay(1000); }

    Wire.begin(PIN_SDA, PIN_SCL, 100000);
    brdPins.addI2C(PinFunction::CODEC, PIN_SCL, PIN_SDA, ES_ADDR, 100000, Wire);
    brdPins.addI2S(PinFunction::CODEC, PIN_MCLK, PIN_BCLK, PIN_WS, PIN_DOUT, PIN_DIN);
    brdPins.addPin(PinFunction::PA, PIN_PA, PinLogic::Output);
    {
        AudioInfo info(44100, 1, 16);
        auto cfg = i2sCodec.defaultConfig(TX_MODE);
        cfg.copyFrom(info);
        cfg.output_device = DAC_OUTPUT_ALL;
        i2sCodec.begin(cfg);
        i2sCodec.setVolume(VOLUME);
    }

    g_serverBase = String(AETHER_URL);
    while (g_serverBase.endsWith("/")) g_serverBase.remove(g_serverBase.length() - 1);

    showScreen("Connecting...", WIFI_SSID_CFG);
    WiFi.begin(WIFI_SSID_CFG, WIFI_PASS_CFG);
    uint32_t ws = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - ws < 20000) { delay(300); yield(); }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] %s\n", WiFi.localIP().toString().c_str());
        showScreen("Waiting...", "Open /video in your browser");
    } else {
        showScreen("WiFi FAILED", "Check voice_config.h");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  loop
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t lastPollMs = 0;

void loop() {
    if (g_playing) { yield(); return; }

    if (millis() - lastPollMs >= POLL_INTERVAL_MS) {
        lastPollMs = millis();

        if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); return; }

        String jobId = checkCurrentJob();
        if (jobId.length() > 0) {
            Serial.printf("[Poll] Job: %s\n", jobId.c_str());

            String title = getJobTitle(jobId);
            if (title.length() > 26) title = title.substring(0, 23) + "...";
            showScreen("Starting...", title.length() > 0 ? title.c_str() : jobId.c_str());

            clearCurrentJob();

            if (!playVideo(jobId)) {
                showScreen("Retrying...", "");
                delay(2000);
                playVideo(jobId);
            }

            lastPollMs = millis();
            showScreen("Waiting...", "Open /video in your browser");
        }
    }

    yield();
}
