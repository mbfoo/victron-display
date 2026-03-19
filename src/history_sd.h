#pragma once
#include <Arduino.h>

// ─── SD card SPI pins (ESP32-C6-Touch-LCD-1.47 schematic) ────────────────────
#define SD_PIN_CLK   1    // shared with LCD SCLK
#define SD_PIN_MOSI  2    // shared with LCD MOSI
#define SD_PIN_MISO  3    // SD only — LCD MISO was -1, now wired to GPIO3
#define SD_PIN_CS    4    // SD chip select only

// ─── API ─────────────────────────────────────────────────────────────────────

// Call once in setup(), AFTER historyInit().
// Mounts the SD card and loads the last snapshot if available.
void historySDInit();

// Call every loop(). Handles the 30-minute timed save.
void historySDTask();

// Force an immediate save to SD. Returns true on success.
bool historySDSave();

// True if SD was successfully mounted during historySDInit().
bool historySDAvailable();
