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

// Display
#define TFT_W 320
#define TFT_H 240

// ── Tuning ──────────────────────────────────────────────────────────
#define VOLUME              0.4f
#define MJPEG_BUF_SIZE       (1024 * 256)
#define PRE_FILL_BYTES       (1024 * 128)
#define STREAM_STALL_MS      15000
#define AUDIO_PRIME_MS       350
#define FALLBACK_FPS         15

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
