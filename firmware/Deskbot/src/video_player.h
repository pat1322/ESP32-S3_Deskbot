#pragma once

#include <Arduino.h>

// Allocates the PSRAM MJPEG buffer and brings up the I2S/ES8311 audio
// path. Call once from setup(), after Wire/I2C pins are available.
void initVideoSubsystem();

// Clamps to [0,1] and applies immediately via the audio codec. Safe to
// call any time after initVideoSubsystem(), including during playback.
void setDeviceVolume(float volume);

// Blocking — identical playback loop to VideoTester.ino's playVideo():
// buffers, spins up the core-0 audio task, then decodes/paces JPEG
// frames until the stream ends or stalls. Returns false on a hard
// stream-open failure (caller may retry).
bool playVideo(const String& jobId, const String& title);
