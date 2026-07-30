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

#define JOB_POLL_INTERVAL_MS  5000
#define TODO_POLL_INTERVAL_MS 45000
#define CLOCK_TICK_MS         1000
#define ANIM_TICK_MS          60
