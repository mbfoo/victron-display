#pragma once

#include <cstdint>

#ifndef MQTT_CONNECT_RETRY_MS
#define MQTT_CONNECT_RETRY_MS 10000
#endif

enum class MqttState {
    MQTT_DISABLED,
    WAITING_FOR_WIFI,
    CONNECTING,
    CONNECTED,
    DISCONNECTED,
};

void      mqttInit();
void      mqttTask();
void      mqttApplyConfig();
void      mqttPublishNow();
MqttState mqttGetState();
bool      mqttIsConnected();
uint32_t  mqttGetPublishCount();
void      mqttPrint();
