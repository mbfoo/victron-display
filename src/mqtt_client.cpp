#include "mqtt_client.h"
#include "config_store.h"
#include "wifi_manager.h"
#include "victron_ble.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include "config.h"

static WiFiClient   s_wifiClient;
static PubSubClient s_mqtt(s_wifiClient);

static MqttState s_state          = MqttState::WAITING_FOR_WIFI;
static uint32_t  s_lastConnectMs  = 0;
static uint32_t  s_lastPublishMs  = 0;
static uint32_t  s_publishCount   = 0;
static bool      s_publishPending = false;

static String topic(const String& sub) {
    String t = String(configGetMqttTopic());
    if (!t.endsWith("/")) t += "/";
    return t + sub;
}

static bool serverConfigured() {
    const char* s = configGetMqttServer();
    return s && strlen(s) > 0 && strcmp(s, "0.0.0.0") != 0;
}

static void pub(const String& t, const String& v, bool retain = false) {
    if (s_mqtt.connected()) s_mqtt.publish(t.c_str(), v.c_str(), retain);
}
static void pub(const String& t, float v, int d = 2) { pub(t, String(v, d)); }
static void pub(const String& t, int32_t v)           { pub(t, String(v)); }
static void pub(const String& t, uint32_t v)          { pub(t, String(v)); }
static void pub(const String& t, bool v)              { pub(t, v ? "1" : "0"); }

static void publishAll() {
    uint8_t n = victronBleGetDeviceCount();
    const VictronMpptData* devs = victronBleGetDevices();

    pub(topic("solar/total_pv_power_w"), victronBleGetTotalPvPower(), 0);
    pub(topic("solar/device_count"),     (int32_t)n);

    for (uint8_t i = 0; i < n; i++) {
        const VictronMpptData& d = devs[i];
        String base = "solar/" + String(i) + "/";
        pub(topic(base + "name"),              String(d.name));
        pub(topic(base + "valid"),             d.valid);
        pub(topic(base + "pv_power_w"),        d.pvPower_W, 0);
        pub(topic(base + "battery_voltage_v"), d.batteryVoltage_V, 2);
        pub(topic(base + "battery_current_a"), d.batteryCurrent_A, 1);
        pub(topic(base + "yield_today_kwh"),   d.yieldToday_kWh, 2);
        pub(topic(base + "charger_state"),     (int32_t)d.chargerState);
        pub(topic(base + "error_code"),        (int32_t)d.errorCode);
        pub(topic(base + "rssi_dbm"),          (int32_t)d.rssi);
    }

    pub(topic("wifi/rssi_dbm"),   (int32_t)wifiGetRssi());
    pub(topic("wifi/uptime_s"),   wifiGetUptime());
    pub(topic("wifi/ip"),         wifiGetIp());
    pub(topic("mcu/uptime_s"),    millis() / 1000UL);
    pub(topic("mcu/free_heap_b"), (uint32_t)ESP.getFreeHeap());
    pub(topic("status"),          "online", true);

    s_publishCount++;
    s_lastPublishMs  = millis();
    s_publishPending = false;
    Serial.printf("[MQTT] Published #%lu\n", s_publishCount);
}

static void beginConnect() {
    const char* srv = configGetMqttServer();
    s_mqtt.setServer(srv, configGetMqttPort());
    s_mqtt.setKeepAlive(60);
    s_mqtt.setSocketTimeout(5);
    String lwt = topic("status");
    bool ok = s_mqtt.connect(CONFIG_MQTT_CLIENT_ID, nullptr, nullptr,
                              lwt.c_str(), 0, true, "offline");
    if (ok) {
        s_state = MqttState::CONNECTED;
        s_publishPending = true;
        Serial.println("[MQTT] Connected");
    } else {
        Serial.printf("[MQTT] Failed rc=%d\n", s_mqtt.state());
        s_state = MqttState::DISCONNECTED;
        s_lastConnectMs = millis();
    }
}

void mqttInit() {
    s_publishCount = s_lastPublishMs = 0;
    s_publishPending = false; s_lastConnectMs = 0;
    if (!configGetMqttEnabled() || !serverConfigured()) {
        s_state = MqttState::MQTT_DISABLED;
        Serial.println("[MQTT] Disabled"); return;
    }
    s_state = MqttState::WAITING_FOR_WIFI;
}

void mqttTask() {
    uint32_t now = millis();
    switch (s_state) {
    case MqttState::MQTT_DISABLED: return;
    case MqttState::WAITING_FOR_WIFI:
        if (wifiIsConnected()) { s_state = MqttState::CONNECTING; beginConnect(); }
        return;
    case MqttState::CONNECTING: return;
    case MqttState::CONNECTED:
        s_mqtt.loop();
        if (!s_mqtt.connected()) { s_state = MqttState::DISCONNECTED; s_lastConnectMs = now; return; }
        if (!wifiIsConnected())  { s_mqtt.disconnect(); s_state = MqttState::WAITING_FOR_WIFI; return; }
        if (s_publishPending ||
            (now - s_lastPublishMs >= (uint32_t)configGetMqttInterval() * 1000UL))
            publishAll();
        return;
    case MqttState::DISCONNECTED:
        if (!wifiIsConnected()) { s_state = MqttState::WAITING_FOR_WIFI; return; }
        if (now - s_lastConnectMs >= MQTT_CONNECT_RETRY_MS) {
            s_state = MqttState::CONNECTING; beginConnect();
        }
        return;
    }
}

void     mqttApplyConfig()     {
    if (s_mqtt.connected()) s_mqtt.disconnect();
    s_state = (!configGetMqttEnabled() || !serverConfigured())
              ? MqttState::MQTT_DISABLED : MqttState::WAITING_FOR_WIFI;
    s_lastConnectMs = 0;
}
void     mqttPublishNow()      { if (s_state == MqttState::CONNECTED) s_publishPending = true; }
MqttState mqttGetState()       { return s_state; }
bool      mqttIsConnected()    { return s_state == MqttState::CONNECTED && s_mqtt.connected(); }
uint32_t  mqttGetPublishCount(){ return s_publishCount; }
void      mqttPrint() {
    static const char* str[] = {"DISABLED","WAIT_WIFI","CONNECTING","CONNECTED","DISCONNECTED"};
    Serial.printf("[MQTT] %s pub=%lu\n", str[(int)s_state], s_publishCount);
}
