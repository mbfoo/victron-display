/**
 * touch_driver.cpp
 * AXS5106L capacitive touch driver for Waveshare ESP32-C6-LCD-1.47
 * Uses Waveshare BSP wrapper: bsp_touch_init / bsp_touch_read / bsp_touch_get_coordinates
 */

#include "touch_driver.h"
#include "lcd_driver.h"   // LCD_WIDTH, LCD_HEIGHT
#include <Arduino.h>
#include <Wire.h>
#include "esp_lcd_touch_axs5106l.h"
#include "display_timeout.h"

// ─── Pins ─────────────────────────────────────────────────────────────────────
#define TOUCH_SDA   18
#define TOUCH_SCL   19
#define TOUCH_INT   21
#define TOUCH_RST   20

// ─── State ────────────────────────────────────────────────────────────────────
static bool    s_available = false;
static int16_t s_lastX     = 0;
static int16_t s_lastY     = 0;

// ─── Public ───────────────────────────────────────────────────────────────────

void touchInit() {
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    bsp_touch_init(&Wire,
                   TOUCH_RST,
                   TOUCH_INT,
                   1,            // rotation 1 = landscape
                   LCD_WIDTH,    // 320
                   LCD_HEIGHT);  // 172
    s_available = true;
    Serial.println("[TOUCH] bsp_touch_init done");
}

void lvglTouchRead(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    if (!s_available) { data->state = LV_INDEV_STATE_REL; return; }

    touch_data_t td;
    bsp_touch_read();
    bool pressed = bsp_touch_get_coordinates(&td);

    if (pressed) {
        s_lastX = td.coords[0].x;
        s_lastY = td.coords[0].y;
        Serial.printf("[TOUCH] x=%d y=%d\n", s_lastX, s_lastY);

        bool wasAsleep = displayTimeoutOnTouch();   // ADD

        data->point.x = s_lastX;
        data->point.y = s_lastY;
        // Suppress first touch to LVGL when waking — don't accidentally tap a widget
        data->state = wasAsleep ? LV_INDEV_STATE_REL : LV_INDEV_STATE_PR;   // CHANGE
    } else {
        data->point.x = s_lastX;
        data->point.y = s_lastY;
        data->state   = LV_INDEV_STATE_REL;
    }
}
