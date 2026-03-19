#pragma once
#include <Arduino.h>

// ─── Timing ───────────────────────────────────────────────────────────────────
#ifndef MQTT_PUBLISH_INTERVAL_MS
  #define MQTT_PUBLISH_INTERVAL_MS   30000    // publish every 30 s
#endif
#ifndef MQTT_CONNECT_RETRY_MS
  #define MQTT_CONNECT_RETRY_MS      10000    // retry connection every 10 s
#endif
#ifndef MQTT_PORT
  #define MQTT_PORT                  1883
#endif

// Client ID shown in the broker — change if running multiple devices
#ifndef MQTT_CLIENT_ID
  #define MQTT_CLIENT_ID             "esp32-bms"
#endif

// ─── State ────────────────────────────────────────────────────────────────────
enum class MqttState {
    MQTT_DISABLED,           // no server configured
    WAITING_FOR_WIFI,   // WiFi not yet connected
    CONNECTING,         // TCP + MQTT connect in progress
    CONNECTED,          // connected and publishing
    DISCONNECTED,       // lost connection, will retry
};

// ─── Init / task ──────────────────────────────────────────────────────────────

/** Call once from setup(). */
void mqttInit();

/** Call every loop(). Non-blocking. */
void mqttTask();

// ─── API ──────────────────────────────────────────────────────────────────────

MqttState   mqttGetState();
bool        mqttIsConnected();
uint32_t    mqttGetPublishCount();      // total successful publish cycles
uint32_t    mqttGetLastPublishMs();     // millis() of last publish
void        mqttApplyConfig();

/** Force an immediate publish (e.g. after a config change or FET toggle). */
void        mqttPublishNow();

/** Print status to Serial. */
void        mqttPrint();
