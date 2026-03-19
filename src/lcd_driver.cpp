// /**
//  * lcd_driver.cpp
//  * ST7789 low-level SPI driver for Waveshare ESP32-C6-LCD-1.47 (172×320)
//  * Landscape via MADCTL=0x20, col offset 34.
//  */

// #include "lcd_driver.h"
// #include <Arduino.h>
// #include <SPI.h>
// #include <SD.h>

// // ─── Pins ─────────────────────────────────────────────────────────────────────
// #define LCD_MOSI      2
// #define LCD_SCLK      1
// // #define LCD_MISO     -1
// #define LCD_CS       14
// #define LCD_DC       15
// #define LCD_RST      22
// #define LCD_BL_FREQ  1000
// #define LCD_BL_RES     10   // 10-bit → 0–1023

// // ─── Panel internals ──────────────────────────────────────────────────────────
// #define LCD_NATIVE_W   172
// #define LCD_OFFSET_X    34
// #define SPI_FREQ    80000000

// #define CAP_W LCD_WIDTH   // 320
// #define CAP_H LCD_HEIGHT  // 172


// static File    s_capFile;
// static bool    s_capturing    = false;
// static bool    s_captureDone  = false;
// static uint8_t s_rowsDone[CAP_H]; // 1 byte per row, 172 bytes total

// // ─────────────────────────────────────────────────────────────────────────────

// static void lcd_writeCmd(uint8_t cmd) {
//     SPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
//     digitalWrite(LCD_CS, LOW);
//     digitalWrite(LCD_DC, LOW);
//     SPI.transfer(cmd);
//     digitalWrite(LCD_CS, HIGH);
//     SPI.endTransaction();
// }

// static void lcd_writeData(uint8_t data) {
//     SPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
//     digitalWrite(LCD_CS, LOW);
//     digitalWrite(LCD_DC, HIGH);
//     SPI.transfer(data);
//     digitalWrite(LCD_CS, HIGH);
//     SPI.endTransaction();
// }

// static void lcd_writeDataBuf(uint8_t* buf, uint32_t len) {
//     SPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
//     digitalWrite(LCD_CS, LOW);
//     digitalWrite(LCD_DC, HIGH);
//     uint8_t dummy[len];
//     SPI.transferBytes(buf, dummy, len);
//     digitalWrite(LCD_CS, HIGH);
//     SPI.endTransaction();
// }

// static void lcd_reset() {
//     digitalWrite(LCD_CS,  LOW);  delay(50);
//     digitalWrite(LCD_RST, LOW);  delay(50);
//     digitalWrite(LCD_RST, HIGH); delay(50);
// }

// static void lcd_setCursor(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
//     lcd_writeCmd(0x2A);
//     lcd_writeData(x1 >> 8);
//     lcd_writeData(x1 & 0xFF);
//     lcd_writeData(x2 >> 8);
//     lcd_writeData(x2 & 0xFF);

//     lcd_writeCmd(0x2B);
//     lcd_writeData((y1 + LCD_OFFSET_X) >> 8);
//     lcd_writeData((y1 + LCD_OFFSET_X) & 0xFF);
//     lcd_writeData((y2 + LCD_OFFSET_X) >> 8);
//     lcd_writeData((y2 + LCD_OFFSET_X) & 0xFF);

//     lcd_writeCmd(0x2C);
// }

// // ─── Public ───────────────────────────────────────────────────────────────────

// void lcdAddWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
//                   uint16_t* color) {
//     uint32_t bytes = (uint32_t)(x2 - x1 + 1) * (y2 - y1 + 1) * 2;
//     lcd_setCursor(x1, y1, x2, y2);
//     lcd_writeDataBuf((uint8_t*)color, bytes);
// }

// void lcdSetBacklight(uint8_t pct) {
//     if (pct > 100) pct = 100;
//     ledcWrite(LCD_BL, (uint32_t)pct * 10);
// }

// void lcdInit() {
//     pinMode(LCD_CS,  OUTPUT);
//     pinMode(LCD_DC,  OUTPUT);
//     pinMode(LCD_RST, OUTPUT);
//     digitalWrite(LCD_CS,  HIGH);
//     digitalWrite(LCD_DC,  HIGH);
//     digitalWrite(LCD_RST, HIGH);

//     ledcAttach(LCD_BL, LCD_BL_FREQ, LCD_BL_RES);
//     lcdSetBacklight(100);

//     SPI.begin(LCD_SCLK, 3, LCD_MOSI);   // MISO=3 for SD card; LCD ignores MISO
//     lcd_reset();

//     lcd_writeCmd(0x11); delay(120);

//     lcd_writeCmd(0x36); lcd_writeData(0x28);   // MADCTL: landscape + BGR panel
//     lcd_writeCmd(0x3A); lcd_writeData(0x05);   // RGB565

//     lcd_writeCmd(0xB0); lcd_writeData(0x00); lcd_writeData(0xE8);
//     lcd_writeCmd(0xB2);
//     lcd_writeData(0x0C); lcd_writeData(0x0C); lcd_writeData(0x00);
//     lcd_writeData(0x33); lcd_writeData(0x33);
//     lcd_writeCmd(0xB7); lcd_writeData(0x35);
//     lcd_writeCmd(0xBB); lcd_writeData(0x35);
//     lcd_writeCmd(0xC0); lcd_writeData(0x2C);
//     lcd_writeCmd(0xC2); lcd_writeData(0x01);
//     lcd_writeCmd(0xC3); lcd_writeData(0x13);
//     lcd_writeCmd(0xC4); lcd_writeData(0x20);
//     lcd_writeCmd(0xC6); lcd_writeData(0x0F);
//     lcd_writeCmd(0xD0); lcd_writeData(0xA4); lcd_writeData(0xA1);
//     lcd_writeCmd(0xD6); lcd_writeData(0xA1);

//     lcd_writeCmd(0xE0);
//     lcd_writeData(0xF0); lcd_writeData(0x00); lcd_writeData(0x04);
//     lcd_writeData(0x04); lcd_writeData(0x04); lcd_writeData(0x05);
//     lcd_writeData(0x29); lcd_writeData(0x33); lcd_writeData(0x3E);
//     lcd_writeData(0x38); lcd_writeData(0x12); lcd_writeData(0x12);
//     lcd_writeData(0x28); lcd_writeData(0x30);

//     lcd_writeCmd(0xE1);
//     lcd_writeData(0xF0); lcd_writeData(0x07); lcd_writeData(0x0A);
//     lcd_writeData(0x0D); lcd_writeData(0x0B); lcd_writeData(0x07);
//     lcd_writeData(0x28); lcd_writeData(0x33); lcd_writeData(0x3E);
//     lcd_writeData(0x36); lcd_writeData(0x14); lcd_writeData(0x14);
//     lcd_writeData(0x29); lcd_writeData(0x32);

//     lcd_writeCmd(0x20);           // inversion OFF
//     lcd_writeCmd(0x11); delay(120);
//     lcd_writeCmd(0x29);           // display ON

//     Serial.println("[LCD] ST7789 init OK");
// }


// /* screenshot capture */
// static void writeU32LE(File& f, uint32_t v) {
//     f.write((uint8_t*)&v, 4);
// }
// static void writeU16LE(File& f, uint16_t v) {
//     f.write((uint8_t*)&v, 2);
// }

// void lcdCaptureBegin() {
//     SD.remove("/data/screenshot.bmp");
//     s_capFile = SD.open("/data/screenshot.bmp", FILE_WRITE);
//     if (!s_capFile) { Serial.println("[CAP] SD open failed"); return; }

//     memset(s_rowsDone, 0, sizeof(s_rowsDone));
//     s_captureDone = false;

//     uint32_t rowBytes  = CAP_W * 3;
//     uint32_t imageSize = rowBytes * CAP_H;
//     uint32_t fileSize  = 54 + imageSize;
//     int32_t  negH      = -(int32_t)CAP_H;   // top-down BMP

//     // BITMAPFILEHEADER
//     s_capFile.write((uint8_t*)"BM", 2);
//     writeU32LE(s_capFile, fileSize);
//     writeU16LE(s_capFile, 0); writeU16LE(s_capFile, 0); // reserved
//     writeU32LE(s_capFile, 54);  // pixel data offset
//     // BITMAPINFOHEADER
//     writeU32LE(s_capFile, 40);  // header size
//     writeU32LE(s_capFile, CAP_W);
//     s_capFile.write((uint8_t*)&negH, 4);
//     writeU16LE(s_capFile, 1);   // planes
//     writeU16LE(s_capFile, 24);  // bpp
//     writeU32LE(s_capFile, 0);   // compression (none)
//     writeU32LE(s_capFile, imageSize);
//     writeU32LE(s_capFile, 2835); writeU32LE(s_capFile, 2835); // ppm
//     writeU32LE(s_capFile, 0);   writeU32LE(s_capFile, 0);

//     // Pre-fill file to full size so we can seek + overwrite tiles
//     uint8_t zero[64] = {};
//     for (uint32_t i = 0; i < imageSize; i += 64)
//         s_capFile.write(zero, min((uint32_t)64, imageSize - i));

//     s_capturing = true;
//     Serial.printf("[CAP] Capture started → %s (%lu bytes)\n", "screenshot.bmp", fileSize);
// }

// void lcdCaptureFlushTile(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
//                           const uint16_t* src) {
//     if (!s_capturing || !s_capFile) return;

//     uint8_t row[CAP_W * 3];
//     for (uint16_t y = y1; y <= y2; y++) {
//         const uint16_t* srcRow = src + (y - y1) * (x2 - x1 + 1);

//         // Build RGB888 row — only fill the tile columns, rest stays zero
//         for (uint16_t x = x1; x <= x2; x++) {
//             uint16_t p = srcRow[x - x1];

//             // Undo LVGL's byte swap to get standard RGB565:
//             // LVGL stores: high=GGGBBBBB  low=RRRRRGGG
//             // After swap:  high=RRRRRGGG  low=GGGBBBBB  ← standard
//             p = (p << 8) | (p >> 8);

//             // BMP on disk is BGR888:
//             row[x*3+0] = ( p        & 0x1F) << 3;  // B
//             row[x*3+1] = ((p >>  5) & 0x3F) << 2;  // G
//             row[x*3+2] = ((p >> 11) & 0x1F) << 3;  // R
//         }

//         // Seek to correct row position in BMP pixel data
//         uint32_t pos = 54 + (uint32_t)y * CAP_W * 3;
//         s_capFile.seek(pos + x1 * 3);
//         s_capFile.write(&row[x1 * 3], (x2 - x1 + 1) * 3);

//         s_rowsDone[y] = 1;
//     }

//     // Check if all rows received
//     bool done = true;
//     for (uint16_t y = 0; y < CAP_H; y++) {
//         if (!s_rowsDone[y]) { done = false; break; }
//     }
//     if (done) {
//         s_capFile.close();
//         s_capturing   = false;
//         s_captureDone = true;
//         Serial.println("[CAP] Capture complete");
//     }
// }

// bool lcdCaptureIsDone() {
//     if (s_captureDone) { s_captureDone = false; return true; }
//     return false;
// }