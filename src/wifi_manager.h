#pragma once
#include <Arduino.h>

// Timing
#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS  15000
#endif
#ifndef WIFI_RETRY_INTERVAL_MS
#define WIFI_RETRY_INTERVAL_MS   10000
#endif
#ifndef WIFI_MONITOR_INTERVAL_MS
#define WIFI_MONITOR_INTERVAL_MS  5000
#endif

enum class WifiState {
    IDLE,
    CONNECTING,
    CONNECTED,
    WAITING_RETRY,
    AP          // soft-AP mode active, STA disabled
};

// Init / task
void wifiInit();
void wifiTask();

// Re-apply config at runtime (call after saving AP/STA settings)
void wifiApplyConfig();

// STA API
WifiState wifiGetState();
bool      wifiIsConnected();   // true only when STA is connected
String    wifiGetIp();
String    wifiGetSsid();
int8_t    wifiGetRssi();       // dBm, 0 when not connected
uint32_t  wifiGetUptime();     // seconds connected, 0 when not connected
void      wifiReconnect();     // force immediate STA reconnect

// AP API
bool      wifiIsAp();          // true when in AP mode
String    wifiGetApIp();       // AP IP (192.168.4.1 typically)

// Debug
void wifiPrint();
