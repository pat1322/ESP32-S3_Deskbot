#include "video_player.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>

#include "pins.h"
#include "display.h"

// TFT_eSPI (pulled in by display.h, above) must be included before
// AudioTools/the audio-driver headers below: the audio-driver library
// defines its own `GPIO` class, which becomes ambiguous with ESP-IDF's
// global `GPIO` register struct if gpio_ll.h gets parsed (via TFT_eSPI)
// after that class is already in scope. Matches VideoTester.ino's
// include order — don't reorder these.
#include "AudioTools.h"
#include "AudioTools/AudioLibs/I2SCodecStream.h"
#if __has_include("AudioTools/AudioCodecs/CodecMP3Helix.h")
  #include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#elif __has_include("AudioCodecs/CodecMP3Helix.h")
  #include "AudioCodecs/CodecMP3Helix.h"
#else
  #error "CodecMP3Helix not found"
#endif

#include "../config.h"

static JPEGDEC       jpeg;
static DriverPins    brdPins;
static AudioBoard    brdDrv(AudioDriverES8311, brdPins);
static I2SCodecStream i2sCodec(brdDrv);

static uint8_t* mjpegBuf = nullptr;

static volatile bool g_playing         = false;
static volatile bool g_audioReady      = false;
static volatile bool g_startPlayback   = false;
static volatile bool g_audioTaskRunning = false;
static String        g_jobId           = "";

// Tier_High: the server's default-quality encode. Tier_Low: a cheaper
// fps/JPEG-compression encode of the same video, generated alongside the
// high tier at queue time (see server/app/services/job_worker.py) so it's
// available instantly on fallback — no on-demand re-encode needed.
// Named Tier_High/Tier_Low (not just High/Low) because Arduino.h defines
// HIGH/LOW as plain #define macros (digital pin levels) that would
// textually clobber unscoped-looking enumerator names at every use site,
// even inside an `enum class`.
enum class VideoTier { Tier_High, Tier_Low };

static const char* tierName(VideoTier t) { return t == VideoTier::Tier_High ? "high" : "low"; }

// Cheap pre-fetch signal for the very first attempt — a real throughput
// measurement isn't available until something is already streaming (see
// the prefill-throughput check in playVideo()), so RSSI is only used to
// pick the starting point. Reuses idle_screen.cpp's "-75dBm = weak"
// threshold so the on-screen signal bars and this decision agree.
static VideoTier pickInitialTier() {
    if (WiFi.status() != WL_CONNECTED) return VideoTier::Tier_Low;
    return (WiFi.RSSI() <= -75) ? VideoTier::Tier_Low : VideoTier::Tier_High;
}

static int jpegDrawCallback(JPEGDRAW* pDraw) {
    tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
    return 1;
}

static String serverBase() {
    String base = String(DESKBOT_SERVER_URL);
    while (base.endsWith("/")) base.remove(base.length() - 1);
    return base;
}

static void audioTaskFn(void*) {
    MP3DecoderHelix    mp3Dec;
    EncodedAudioStream decoded(&i2sCodec, &mp3Dec);
    mp3Dec.begin();
    decoded.begin();

    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setConnectionTimeout(15000);

    HTTPClient http;
    http.begin(cli, serverBase() + "/video/stream/" + g_jobId + ".mp3");
    http.addHeader("X-Api-Key", DESKBOT_API_KEY);
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
    g_audioTaskRunning = false;
    vTaskDelete(NULL);
}

void initVideoSubsystem() {
    if (psramFound()) {
        mjpegBuf = (uint8_t*)ps_malloc(MJPEG_BUF_SIZE);
        Serial.printf("[Init] PSRAM MJPEG buf: %d KB\n", MJPEG_BUF_SIZE / 1024);
    } else {
        mjpegBuf = (uint8_t*)malloc(MJPEG_BUF_SIZE);
        Serial.println("[Init] No PSRAM — heap");
    }
    if (!mjpegBuf) { showScreen("MEM ERROR"); while (1) delay(1000); }

    if (psramFound()) {
        Serial.printf("[Init] PSRAM free/total: %u / %u KB\n",
                       ESP.getFreePsram() / 1024, ESP.getPsramSize() / 1024);
    }

    pinMode(PIN_PA, OUTPUT); digitalWrite(PIN_PA, LOW);

    Wire.begin(PIN_SDA, PIN_SCL, 100000);
    brdPins.addI2C(PinFunction::CODEC, PIN_SCL, PIN_SDA, ES_ADDR, 100000, Wire);
    brdPins.addI2S(PinFunction::CODEC, PIN_MCLK, PIN_BCLK, PIN_WS, PIN_DOUT, PIN_DIN);
    brdPins.addPin(PinFunction::PA, PIN_PA, PinLogic::Output);

    AudioInfo info(44100, 1, 16);
    auto cfg = i2sCodec.defaultConfig(TX_MODE);
    cfg.copyFrom(info);
    cfg.output_device = DAC_OUTPUT_ALL;
    i2sCodec.begin(cfg);
    i2sCodec.setVolume(VOLUME);
}

void setDeviceVolume(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    i2sCodec.setVolume(volume);
}

// Blocks until g_audioTaskRunning goes false (audioTaskFn has fully torn
// down and deleted itself) or a generous timeout elapses. Must be called
// after g_playing is set false and before spinning up a fresh audio task
// for a restart — otherwise two tasks could briefly both drive the same
// static i2sCodec/decoded objects.
static void waitForAudioTaskTeardown() {
    uint32_t t0 = millis();
    while (g_audioTaskRunning && millis() - t0 < 1000) { delay(10); yield(); }
}

bool playVideo(const String& jobId, const String& title) {
    g_jobId = jobId;

    showScreen("Buffering...", title.length() > 0 ? title.c_str() : "Opening stream");

    VideoTier tier = pickInitialTier();
    bool usedPrePlaybackCorrection = false;
    bool usedMidStreamDowngrade    = false;
    uint32_t totalFrameCount       = 0;

    // At most 3 attempts: the initial one, plus one optional pre-playback
    // correction (weak prefill throughput on the high tier) and one
    // optional mid-stream downgrade (falls behind badly once playing) —
    // usedPrePlaybackCorrection/usedMidStreamDowngrade cap each to firing
    // once, so this loop never actually reaches 3 restarts in practice.
    for (int attempt = 0; attempt < 3; attempt++) {
        g_playing       = true;
        g_audioReady    = false;
        g_startPlayback = false;

        Serial.printf("[Video] Attempt %d, tier=%s\n", attempt, tierName(tier));

        WiFiClientSecure vcli;
        vcli.setInsecure();
        vcli.setConnectionTimeout(20000);

        HTTPClient vhttp;
        vhttp.begin(vcli, serverBase() + "/video/stream/" + jobId + ".mjpeg?tier=" + tierName(tier));
        vhttp.addHeader("X-Api-Key", DESKBOT_API_KEY);
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

        uint8_t streamFps = FALLBACK_FPS;
        vs->readBytes(&streamFps, 1);
        if (streamFps == 0 || streamFps > 60) streamFps = FALLBACK_FPS;
        const uint32_t frameMs = 1000 / streamFps;
        Serial.printf("[Video] %u fps  frameMs=%u ms\n", streamFps, frameMs);

        int bytesInBuf = 0;
        tft.fillScreen(TFT_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(max(0, (TFT_W - 12 * 12) / 2), TFT_H / 2 - 22);
        tft.print("Buffering...");

        int lastBarPct         = -1;
        uint32_t prefillStartMs = millis();
        uint32_t prefillDeadline = prefillStartMs + 8000;
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
        uint32_t prefillMs = max((uint32_t)1, millis() - prefillStartMs);
        float    prefillBps = bytesInBuf * 1000.0f / prefillMs;
        Serial.printf("[Video] Pre-filled %d bytes in %u ms (%.0f B/s)\n", bytesInBuf, prefillMs, prefillBps);

        // Weak connection caught before a single frame has been drawn —
        // restart at the low tier now, invisibly to the user.
        if (tier == VideoTier::Tier_High && !usedPrePlaybackCorrection && prefillBps < VIDEO_PREFILL_MIN_BPS_HIGH) {
            Serial.println("[Video] Prefill throughput too low for high tier, switching to low");
            usedPrePlaybackCorrection = true;
            tier = VideoTier::Tier_Low;
            g_playing = false;
            vhttp.end();
            continue;
        }

        showScreen("Buffering...", "Starting audio...");
        g_audioTaskRunning = true;
        xTaskCreatePinnedToCore(audioTaskFn, "vid_audio", 32768, NULL, 2, NULL, 0);
        uint32_t t0 = millis();
        while (!g_audioReady && millis() - t0 < 8000) { delay(50); yield(); }

        tft.fillScreen(TFT_BLACK);
        showStatus("Playing", COLOR_AMBER);
        g_startPlayback = true;

        delay(AUDIO_PRIME_MS);
        uint32_t playStartMs = millis();
        uint32_t frameCount  = 0;
        uint32_t lastByteMs  = millis();
        uint32_t fpsFrames   = 0;
        uint32_t fpsWindowMs = millis();

        uint32_t discardedFrames        = 0;
        int      consecutiveDiscards    = 0;
        bool     needsMidStreamDowngrade = false;

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

                // Prefer staying on schedule (drop frames as soon as we
                // fall meaningfully behind, rather than letting a backlog
                // build), but never let more than
                // VIDEO_FORCE_DRAW_MAX_SKIP consecutive frames go by
                // undrawn — otherwise sustained low throughput makes
                // elapsedMs permanently outpace expectedMs and the screen
                // simply stops updating forever ("stuck on one frame").
                // Forcing an occasional draw guarantees visible progress
                // regardless of how far behind the pacing math says we are.
                bool onSchedule = elapsedMs <= expectedMs + frameMs * 3;
                bool forceDraw  = !onSchedule && consecutiveDiscards >= VIDEO_FORCE_DRAW_MAX_SKIP;

                if (onSchedule || forceDraw) {
                    if (onSchedule) {
                        while (millis() - playStartMs < expectedMs) yield();
                    }

                    int frameSize = frameEnd - frameStart + 1;
                    if (jpeg.openRAM(mjpegBuf + frameStart, frameSize, jpegDrawCallback)) {
                        jpeg.decode(
                            max(0, (TFT_W - jpeg.getWidth())  / 2),
                            max(0, (TFT_H - jpeg.getHeight()) / 2),
                            0
                        );
                        jpeg.close();
                    }
                    consecutiveDiscards = 0;
                } else {
                    consecutiveDiscards++;
                    discardedFrames++;
                }

                int remaining = bytesInBuf - frameEnd - 1;
                if (remaining > 0) memmove(mjpegBuf, mjpegBuf + frameEnd + 1, remaining);
                bytesInBuf = max(0, remaining);

                if (millis() - fpsWindowMs >= 5000) {
                    float skipRatio = fpsFrames > 0 ? (float)discardedFrames / fpsFrames : 0.0f;
                    Serial.printf("[Video] %.1f fps, %.0f%% skipped (tier=%s)\n",
                                  fpsFrames / 5.0f, skipRatio * 100, tierName(tier));
                    if (tier == VideoTier::Tier_High && !usedMidStreamDowngrade && skipRatio > VIDEO_SKIP_RATIO_DOWNGRADE) {
                        needsMidStreamDowngrade = true;
                    }
                    fpsFrames       = 0;
                    discardedFrames = 0;
                    fpsWindowMs     = millis();
                }

                if (needsMidStreamDowngrade) break;

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
        waitForAudioTaskTeardown();
        vhttp.end();
        totalFrameCount += frameCount;

        if (needsMidStreamDowngrade) {
            Serial.println("[Video] Falling behind on high tier, restarting at low");
            showScreen("Adjusting...", "Switching to a lower quality stream");
            usedMidStreamDowngrade = true;
            tier = VideoTier::Tier_Low;
            continue;
        }

        Serial.printf("[Video] Done — %u frames\n", totalFrameCount);
        return true;
    }

    Serial.printf("[Video] Done — %u frames\n", totalFrameCount);
    return true;
}
