#pragma once
#include <stdint.h>

// ─── Panel geometry (used by other modules) ───────────────────────────────────
#define LCD_WIDTH    320
#define LCD_HEIGHT   172
#define LCD_BL        23

void lcdInit();
void lcdAddWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color);
void lcdSetBacklight(uint8_t pct);   // 0–100

// Capture control
void lcdCaptureBegin();  // open SD file, write header
void lcdCaptureFlushTile(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                         const uint16_t* src);
bool lcdCaptureIsDone();                 // true once all rows received