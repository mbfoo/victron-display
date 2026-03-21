#include "watchdog.h"
#include <esp_task_wdt.h>
#include <Arduino.h>

static uint32_t s_initMs = 0;

void watchdogInit() {
    esp_task_wdt_deinit();

    s_initMs = millis();
    const esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms    = WDT_TIMEOUT_MS,
        .idle_core_mask = (1 << 0),   // watch CPU0; add (1<<1) for dual-core
        .trigger_panic  = true
    };
    esp_task_wdt_init(&wdt_cfg);
    esp_task_wdt_add(NULL);
    Serial.printf("[WDT] Started (%d s)\n", WDT_TIMEOUT_MS / 1000);
}

void     watchdogTask()        { esp_task_wdt_reset(); }
uint32_t watchdogGetUptimeMs() { return millis() - s_initMs; }
void     watchdogPrint()       {
    Serial.printf("[WDT] Uptime: %lu s\n", watchdogGetUptimeMs() / 1000UL);
}
