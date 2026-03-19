#include <Arduino.h>
#include "jbd_bms.h"
#include "config_store.h"
#include "wifi_manager.h"
#include "battery_estimator.h"
#include "soc_limiter.h"
#include "web_server.h"
#include "watchdog.h"
#include "mqtt_client.h"
#include "display.h"
#include "history.h"
#include "history_sd.h"
#include "display_timeout.h"


void setup() {
    Serial.begin(115200);
    Serial.println("[MAIN] Booting...");

    
    // Config must be loaded before any module that depends on it
    configInit();
    configPrint();
    
    displayInit();    // show splash screen early

    watchdogInit();   // start HW watchdog early so hangs during init are caught

    wifiInit();

    bmsInit(configGetBmsDeviceName(), configGetBmsPollInterval());   // device name fragment, poll interval ms

    historyInit();
    historySDInit();   // mounts SD and loads snapshot into ring buffers

    batEstInit(configGetEstTau());
    socLimiterInit();
    webServerInit();
    mqttInit();
    displayTimeoutInit();

}

void loop() {
    watchdogTask();   // feed HW WDT + check BMS timeout — must be first
    // wifiSetEnabled(bmsGetState() == BmsState::CONNECTED); // WiFi (and everything that depends on it) is only active while the BMS is connected. On disconnect the radio is powered off immediately.
    // wifiSetEnabled(true);
    wifiTask();   // non-blocking WiFi state machine
    bmsTask();   // non-blocking – drives all BMS BLE logic
    historyTask();
    historySDTask();   // handles timed saves
    batEstTask();   // non-blocking, recalculates every BAT_EST_UPDATE_INTERVAL_MS
    socLimiterTask();   // evaluates every SOC_LIMITER_CHECK_INTERVAL_MS
    webServerTask();   // non-blocking handleClient()
    mqttTask();
    displayTask();    // refreshes every 1 s, non-blocking
    displayTimeoutTask();


    // ── Example: consume BMS data from another module ────────────────────────
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 20000) {
        Serial.printf("[MAIN] BMS State: %d\n", bmsGetState());
        wifiPrint();
        batEstPrint();
        socLimiterPrint();
        watchdogPrint();
        mqttPrint();
        historyPrint();
        lastPrint = millis();
        
        if (bmsIsDataValid()) {
            Serial.printf("[MAIN] %.2fV  %.2fA  SOC=%d%%  T=%.1fC  Cell1=%dmV\n",
                        bmsGetVoltage(),
                        bmsGetCurrent(),
                        bmsGetSOC(),
                        bmsGetTemperature(0),
                        bmsGetCellVoltage(0));
        }
    }

}
