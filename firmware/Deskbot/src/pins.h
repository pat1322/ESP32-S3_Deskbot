#pragma once

// Wiring is identical to VideoTester.ino — same board, same connections.
// TFT SPI pins are configured in the TFT_eSPI library's own User_Setup.h
// (not here) and are left untouched; we only call tft.init()/setRotation().

// I2C (ES8311 audio codec)
#define PIN_SDA   1
#define PIN_SCL   2
#define ES_ADDR   0x18

// I2S (ES8311 audio codec)
#define PIN_MCLK  38
#define PIN_BCLK  14
#define PIN_WS    13
#define PIN_DOUT  45
#define PIN_DIN   12
#define PIN_PA    48

// Display — no TFT_W/TFT_H here: the physical panel is fixed 320x240, but
// which axis is "width" vs "height" depends on the live orientation
// (display.cpp's displaySetOrientation()), so every draw call reads
// tft.width()/tft.height() fresh instead of a compile-time constant.

// ── Tuning ──────────────────────────────────────────────────────────
#define VOLUME              0.4f
#define MJPEG_BUF_SIZE       (1024 * 256)
#define PRE_FILL_BYTES       (1024 * 128)
#define STREAM_STALL_MS      15000
#define AUDIO_PRIME_MS       350
#define FALLBACK_FPS         15
// How often playVideo() asks the server whether its job is still
// "playing" (a website cancel flips this) so a cancel stops the screen
// within a few seconds instead of only when the video naturally ends.
#define VIDEO_CANCEL_CHECK_MS 3000
// A single 320x240 JPEG at extract_still's quality (server/app/services/
// ffmpeg_service.py) is tens of KB — nowhere near MJPEG_BUF_SIZE above, so
// photo_view.cpp gets its own much smaller buffer rather than reusing
// video_player.cpp's.
#define PHOTO_BUF_SIZE       (128 * 1024)

#define JOB_POLL_INTERVAL_MS          5000
#define DEVICE_STATE_POLL_INTERVAL_MS 45000
#define CLOCK_TICK_MS                 1000
#define ANIM_TICK_MS                  60

// ── Adaptive video quality ─────────────────────────────────────────
// Tune these against your real-world WiFi using the serial fps/skip-ratio
// log ("[Video] N fps, N% skipped") — starting points, not measured values.
//
// Bytes/sec observed filling the pre-fill buffer on the high tier; below
// this, playVideo() restarts at the low tier before drawing a single frame.
#define VIDEO_PREFILL_MIN_BPS_HIGH   40000
// Fraction of frames discarded (not drawn) in a rolling 5s window that
// triggers a one-time mid-stream downgrade from high to low tier.
#define VIDEO_SKIP_RATIO_DOWNGRADE   0.40f
// Never go more than this many consecutively-arrived frames without a
// forced decode+draw, even when badly behind schedule — guarantees the
// screen keeps advancing instead of freezing on one frame.
#define VIDEO_FORCE_DRAW_MAX_SKIP    3

// ── Audio resync ─────────────────────────────────────────────────────
// video_player.cpp's video decode loop above actively paces itself
// against wall-clock time and skips frames to catch up if it falls
// behind. audioTaskFn has no equivalent — it just plays back whatever MP3
// bytes arrive, in order — so a starved audio stream (e.g. the video
// stream on the same WiFi radio eating most of the throughput) drifts
// later and later with nothing to bring it back for the rest of the
// video. These two constants give it the same kind of catch-up ability.
//
// server/app/services/ffmpeg_service.py's extract_audio() always encodes
// a fixed 96kbps mono CBR MP3, so bytes written to the decoder convert
// directly to represented playback time — no need to parse MP3 frame
// headers to know how far into the audio a given byte offset represents.
#define AUDIO_BYTES_PER_MS       (96000 / 8 / 1000)
// If audio falls this far behind real elapsed time, skip ahead (discard
// incoming MP3 bytes without decoding them) instead of letting the gap
// grow for the rest of the video. Deliberately more generous than
// video's own few-hundred-ms tolerance — skipping audio is a more
// noticeable, disruptive correction (a brief silent gap) than dropping
// one video frame, so it shouldn't fire on ordinary jitter, only on a
// real, sustained stall.
#define AUDIO_RESYNC_THRESHOLD_MS 700
