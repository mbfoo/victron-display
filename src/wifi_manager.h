#pragma once
#include <Arduino.h>

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS  15000
#endif
#ifndef WIFI_RETRY_INTERVAL_MS
#define WIFI_RETRY_INTERVAL_MS   10000
#endif
#ifndef WIFI_MONITOR_INTERVAL_MS
#define WIFI_MONITOR_INTERVAL_MS  5000
#endif

enum class WifiState { IDLE, CONNECTING, CONNECTED, WAITING_RETRY, AP };

void      wifiInit();
void      wifiTask();
void      wifiApplyConfig();
WifiState wifiGetState();
bool      wifiIsConnected();
bool      wifiIsAp();
String    wifiGetIp();
String    wifiGetApIp();
String    wifiGetSsid();
int8_t    wifiGetRssi();
uint32_t  wifiGetUptime();
void      wifiReconnect();
void      wifiPrint();
