/*
 * main.cpp — Victron MPPT BLE Monitor for ESP32-C6
 *
 * Current target:  ESP32-C6-DevKitC-1  (no display)
 * Future target:   Waveshare ESP32-C6 Touch 1.47" — enable display then
 */

#include <Arduino.h>
#include "config_store.h"
#include "victron_ble.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "web_server.h"
#include "watchdog.h"
#include "display.h"    // stubs only

void setup() {
    Serial.begin(115200);
    delay(5000);
    Serial.println("[MAIN] Booting Victron MPPT Monitor...");

    configInit();
    configPrint();

    displayInit();

    watchdogInit();

    // BLE init first: NimBLE + WiFi coexistence on ESP32-C6
    victronBleInit();

    wifiInit();
    webServerInit();
    mqttInit();

    Serial.println("[MAIN] Setup complete");
}

void loop() {
    watchdogTask();     // feed WDT — must be first

    victronBleTask();   // scan health + timeout detection

    wifiTask();
    webServerTask();
    mqttTask();

    displayTask();

    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 30000) {
        lastPrint = millis();
        wifiPrint();
        victronBlePrint();
        mqttPrint();
        watchdogPrint();
    }
}
