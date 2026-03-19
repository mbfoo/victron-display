#include "display_timeout.h"
#include "config_store.h"
#include "lcd_driver.h"

static uint32_t s_lastTouchMs = 0;
static bool     s_displayOn   = true;

// 65535 is stored as sentinel for 24 h
static uint32_t timeoutMs() {
    uint16_t v = configGetDisplayTimeout();
    if (v == 0)     return 0;               // never
    if (v == 65535) return 86400000UL;      // 24 h
    return (uint32_t)v * 1000UL;
}

static void turnOff() {
    if (!s_displayOn) return;
    s_displayOn = false;
    lcdSetBacklight(0);
    Serial.println("[DISP] Timeout: sleep");
}

static void turnOn() {
    if (s_displayOn) return;
    s_displayOn = true;
    lcdSetBacklight(configGetBacklight());
    Serial.println("[DISP] Timeout: wake");
}

void displayTimeoutInit() {
    s_lastTouchMs = millis();
    s_displayOn   = true;
}

void displayTimeoutTask() {
    uint32_t ms = timeoutMs();
    if (ms == 0) return;
    if (s_displayOn && (millis() - s_lastTouchMs >= ms))
        turnOff();
}

bool displayTimeoutOnTouch() {
    s_lastTouchMs = millis();
    if (!s_displayOn) {
        turnOn();
        return true;   // wake-only touch — caller must suppress to LVGL
    }
    return false;
}

bool displayTimeoutIsOn() { return s_displayOn; }
