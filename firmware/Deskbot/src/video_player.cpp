#include "video_player.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <ArduinoJson.h>

#include "pins.h"
#include "display.h"
#include "remote_log.h"

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

// Kept global rather than local to audioTaskFn (as it was before) so they're
// never reconstructed on every playback attempt/restart — a WiFiClientSecure
// TLS session and the Helix MP3 decoder's internal working buffer are each a
// few KB of heap, and recreating them fresh on every attempt/downgrade-retry
// fragments the heap over a long-running session. Mirrors the same pattern
// already used for the video-side JPEGDEC/I2SCodecStream globals above.
static WiFiClientSecure   g_audioCli;
static MP3DecoderHelix    g_audioMp3Dec;
static EncodedAudioStream g_audioDecoded(&i2sCodec, &g_audioMp3Dec);

// Used only for the periodic mid-playback cancel check below — kept global
// for the same reason as g_audioCli above: it's reused roughly every 3s for
// the entire length of a video, so a stack-local WiFiClientSecure here would
// churn its TLS session buffers dozens of times per video.
static WiFiClientSecure g_statusCli;

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

// Polled roughly every VIDEO_CANCEL_CHECK_MS during playback so a
// website-initiated cancel (which only updates the job's status
// server-side — the device has no other way to find out) stops the
// screen within a few seconds instead of playing the whole video out.
// Defaults to true (keep playing) on any request/parse failure, so a
// transient network hiccup can't stop playback on its own.
static bool isJobStillPlaying(const String& jobId) {
    g_statusCli.setInsecure();
    g_statusCli.setConnectionTimeout(4000);
    HTTPClient http;
    http.begin(g_statusCli, serverBase() + "/video/status/" + jobId);
    http.addHeader("X-Api-Key", DESKBOT_API_KEY);
    http.setTimeout(4000);
    int code = http.GET();
    if (code != 200) { http.end(); g_statusCli.stop(); return true; }
    String resp = http.getString();
    http.end();
    g_statusCli.stop();

    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return true;
    const char* status = doc["status"] | "playing";
    return strcmp(status, "playing") == 0;
}

static void audioTaskFn(void*) {
    g_audioMp3Dec.begin();
    g_audioDecoded.begin();

    g_audioCli.setInsecure();
    g_audioCli.setConnectionTimeout(15000);

    HTTPClient http;
    String audioUrl = serverBase() + "/video/stream/" + g_jobId + ".mp3";

    // A restart (mid-stream tier downgrade, or the pre-playback high->low
    // correction) tears down and reopens the video-side TLS connection at
    // almost the same moment this task reopens its own — back-to-back TLS
    // handshakes under that heap/radio pressure occasionally drop the first
    // attempt. Without a retry here, that single failure used to be
    // permanent: the fallback below marks itself "ready" either way so
    // video playback is never blocked on audio, which meant a restart could
    // silently play the rest of the video with no sound and no visible
    // error (only a Serial print, easy to miss).
    int code = -1;
    for (int attempt = 0; attempt < 3 && g_playing; attempt++) {
        if (attempt > 0) {
            http.end();
            g_audioCli.stop();
            delay(300);
        }
        http.begin(g_audioCli, audioUrl);
        http.addHeader("X-Api-Key", DESKBOT_API_KEY);
        http.setTimeout(600000);
        code = http.GET();
        Serial.printf("[Audio] HTTP %d (attempt %d)\n", code, attempt);
        if (code == 200) break;
    }

    if (code == 200) {
        WiFiClient* s = http.getStreamPtr();
        g_audioReady = true;

        // Drain (and discard) audio bytes while video prefills, instead of
        // leaving the socket completely unread — an unread receive buffer
        // here can backpressure/stall the server's write for however long
        // video buffering takes (several seconds), occasionally leaving the
        // connection in a bad state before playback even starts.
        uint8_t drainBuf[512];
        while (!g_startPlayback && g_playing) {
            int avail = s->available();
            if (avail > 0) {
                s->readBytes(drainBuf, min(avail, (int)sizeof(drainBuf)));
            } else {
                vTaskDelay(1);
            }
        }

        // From here on, audio content should track real elapsed time 1:1.
        // audioStartMs is captured locally rather than shared with the
        // video loop's playStartMs (in playVideo(), a different function)
        // on purpose — this only needs to know when ITS OWN decoding
        // began, so there's no cross-task race to worry about.
        uint32_t audioStartMs = millis();
        uint32_t contentBytes = 0;

        // Set while a resync discard is in progress. While true, chunks
        // are dropped whole rather than written to the decoder. The bug
        // this guards against: resuming a write on the very next chunk
        // after a discard almost never lands on an MP3 frame boundary
        // (frames are ~144-150 bytes, chunks are read in up-to-2048-byte
        // increments off the socket) — feeding the Helix decoder a
        // frame-unaligned bitstream desyncs its parser, which is what
        // produced the cracking/wrong-pitch audio this fixes. Once caught
        // up, don't resume writing blindly — scan for the next real frame
        // sync word first (see below) so the decoder only ever starts on
        // a valid frame boundary.
        bool audioResyncing = false;

        uint8_t buf[2048];
        while (g_playing) {
            int avail = s->available();
            if (avail > 0) {
                int got = s->readBytes(buf, min(avail, (int)sizeof(buf)));
                if (got > 0) {
                    uint32_t elapsedMs = millis() - audioStartMs;
                    uint32_t contentMs = contentBytes / AUDIO_BYTES_PER_MS;
                    if (elapsedMs > contentMs + AUDIO_RESYNC_THRESHOLD_MS) {
                        // Fallen too far behind real time — discard this
                        // chunk instead of decoding it, to skip ahead
                        // rather than let the gap grow into audible
                        // lip-sync drift for the rest of the video.
                        if (!audioResyncing) {
                            Serial.printf("[Audio] Resyncing, %u ms behind\n", elapsedMs - contentMs);
                        }
                        audioResyncing = true;
                    } else if (audioResyncing) {
                        // Just caught up. Find the next MP3 frame sync
                        // word (0xFF followed by a byte with its top 3
                        // bits set) in this chunk and only start writing
                        // from there, discarding any leading partial-frame
                        // bytes before it — never resume mid-frame.
                        int syncOffset = -1;
                        for (int i = 0; i < got - 1; i++) {
                            if (buf[i] == 0xFF && (buf[i + 1] & 0xE0) == 0xE0) {
                                syncOffset = i;
                                break;
                            }
                        }
                        if (syncOffset >= 0) {
                            g_audioDecoded.write(buf + syncOffset, got - syncOffset);
                            audioResyncing = false;
                        }
                        // else: no sync word in this chunk either, keep
                        // discarding and try again on the next one.
                    } else {
                        g_audioDecoded.write(buf, got);
                    }
                    contentBytes += got;
                }
            } else {
                if (!http.connected() && s->available() == 0) break;
                vTaskDelay(1);
            }
        }
    } else {
        g_audioReady = true;
    }

    g_audioDecoded.end();
    http.end();
    g_audioCli.stop();
    g_playing = false;
    Serial.println("[Audio] Done");
    g_audioTaskRunning = false;
    vTaskDelete(NULL);
}

void initVideoSubsystem() {
    if (psramFound()) {
        mjpegBuf = (uint8_t*)ps_malloc(MJPEG_BUF_SIZE);
        remoteLog("[Init] PSRAM MJPEG buf: %d KB", MJPEG_BUF_SIZE / 1024);
    } else {
        mjpegBuf = (uint8_t*)malloc(MJPEG_BUF_SIZE);
        remoteLog("[Init] No PSRAM - heap");
    }
    if (!mjpegBuf) { showScreen("MEM ERROR"); while (1) delay(1000); }

    if (psramFound()) {
        remoteLog("[Init] PSRAM free/total: %u / %u KB",
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
    remoteLogFlush(); // clear anything already queued before this potentially-fatal call

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

        remoteLog("[Video] Attempt %d, tier=%s", attempt, tierName(tier));

        WiFiClientSecure vcli;
        vcli.setInsecure();
        vcli.setConnectionTimeout(20000);

        HTTPClient vhttp;
        vhttp.begin(vcli, serverBase() + "/video/stream/" + jobId + ".mjpeg?tier=" + tierName(tier));
        vhttp.addHeader("X-Api-Key", DESKBOT_API_KEY);
        vhttp.setTimeout(600000);

        int code = vhttp.GET();
        remoteLog("[Video] HTTP %d", code);
        remoteLogFlush();

        if (code != 200) {
            remoteLog("[Video] Stream open failed, giving up (HTTP %d)", code);
            remoteLogFlush();
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
        remoteLog("[Video] %u fps  frameMs=%u ms", streamFps, frameMs);

        int bytesInBuf = 0;
        tft.fillScreen(TFT_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(max(0, (tft.width() - 12 * 12) / 2), tft.height() / 2 - 22);
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
        remoteLog("[Video] Pre-filled %d bytes in %u ms (%.0f B/s)", bytesInBuf, prefillMs, prefillBps);
        remoteLogFlush();

        // Weak connection caught before a single frame has been drawn —
        // restart at the low tier now, invisibly to the user.
        if (tier == VideoTier::Tier_High && !usedPrePlaybackCorrection && prefillBps < VIDEO_PREFILL_MIN_BPS_HIGH) {
            remoteLog("[Video] Prefill throughput too low for high tier, switching to low");
            remoteLogFlush();
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
        uint32_t lastCancelCheckMs      = millis();

        // Read-position pointer into mjpegBuf, instead of memmove-ing the
        // whole remaining buffer after every single frame: memmove-per-frame
        // on a buffer this size costs real per-frame time and was a direct
        // contributor to the skip/stutter behavior this loop otherwise
        // fights via the schedule/force-draw logic below. Only compact
        // (memmove) once readPos has consumed past half of MJPEG_BUF_SIZE,
        // amortizing that cost across many frames instead of paying it once
        // per frame.
        int readPos = 0;

        while (g_playing) {

            if (millis() - lastCancelCheckMs >= VIDEO_CANCEL_CHECK_MS) {
                lastCancelCheckMs = millis();
                if (!isJobStillPlaying(jobId)) {
                    remoteLog("[Video] Cancelled from website, stopping");
                    remoteLogFlush();
                    break;
                }
            }

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
            for (int i = readPos; i < bytesInBuf - 1; i++) {
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
                        // Center-crop rather than clamp to 0: in portrait
                        // (240 wide) a landscape-sourced frame (e.g. 320
                        // wide) is wider than the screen, and a negative
                        // origin here shifts it left so the same amount
                        // gets cropped off both edges instead of only the
                        // right/bottom. Relies on TFT_eSPI's pushImage
                        // (called from jpegDrawCallback) clipping negative
                        // coordinates safely — needs on-device
                        // verification; if it misbehaves, fall back to
                        // max(0, ...) like before (crops one edge only).
                        jpeg.decode(
                            (tft.width()  - jpeg.getWidth())  / 2,
                            (tft.height() - jpeg.getHeight()) / 2,
                            0
                        );
                        jpeg.close();
                    }
                    consecutiveDiscards = 0;
                } else {
                    consecutiveDiscards++;
                    discardedFrames++;
                }

                // Advance past the consumed frame; only compact the buffer
                // once readPos has eaten past its halfway point.
                readPos = frameEnd + 1;
                if (readPos > MJPEG_BUF_SIZE / 2) {
                    int remaining = bytesInBuf - readPos;
                    if (remaining > 0) memmove(mjpegBuf, mjpegBuf + readPos, remaining);
                    bytesInBuf = max(0, remaining);
                    readPos = 0;
                }

                if (millis() - fpsWindowMs >= 5000) {
                    float skipRatio = fpsFrames > 0 ? (float)discardedFrames / fpsFrames : 0.0f;
                    remoteLog("[Video] %.1f fps, %.0f%% skipped (tier=%s), free heap=%u",
                              fpsFrames / 5.0f, skipRatio * 100, tierName(tier), ESP.getFreeHeap());
                    remoteLogFlush();
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
                    remoteLog("[Video] Stream ended (frames this attempt: %u)", frameCount);
                    break;
                }
                yield();
            }
        }

        g_playing = false;
        waitForAudioTaskTeardown();
        vhttp.end();
        totalFrameCount += frameCount;
        remoteLogFlush();

        if (needsMidStreamDowngrade) {
            remoteLog("[Video] Falling behind on high tier, restarting at low");
            remoteLogFlush();
            showScreen("Adjusting...", "Switching to a lower quality stream");
            usedMidStreamDowngrade = true;
            tier = VideoTier::Tier_Low;
            continue;
        }

        remoteLog("[Video] Done - %u frames", totalFrameCount);
        remoteLogFlush();
        return true;
    }

    remoteLog("[Video] Done - %u frames", totalFrameCount);
    remoteLogFlush();
    return true;
}
