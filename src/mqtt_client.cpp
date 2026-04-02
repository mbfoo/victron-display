#include "mqtt_client.h"
#include "config_store.h"
#include "wifi_manager.h"
#include "victron_ble.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include "config.h"
#include "victron_ble.h"


static WiFiClient        s_wifiClient;
static WiFiClientSecure* s_wifiClientSecure = nullptr;
static PubSubClient      s_mqtt(s_wifiClient);

static MqttState s_state          = MqttState::WAITING_FOR_WIFI;
static uint32_t  s_lastConnectMs  = 0;
static uint32_t  s_lastPublishMs  = 0;
static uint32_t  s_publishCount   = 0;
static bool      s_publishPending = false;

static char s_caCert[MQTT_CA_CERT_MAX_LEN];
static char s_topicBuf[128];

static const char* tp(const char* subtopic) {
    snprintf(s_topicBuf, sizeof(s_topicBuf), "%s/%s",
             configGetMqttTopic(), subtopic);
    return s_topicBuf;
}

static String topic(const String& sub) {
    String t = String(configGetMqttTopic());
    if (!t.endsWith("/")) t += "/";
    return t + sub;
}

static bool serverConfigured() {
    const char* s = configGetMqttServer();
    return s && strlen(s) > 0 && strcmp(s, "0.0.0.0") != 0;
}

static void pub(const char* t, const char* payload, bool retain = false) {
    if (!s_mqtt.connected()) return;
    if (!s_mqtt.publish(t, payload, retain))
        Serial.printf("[MQTT] Publish failed: %s\n", t);
}

static void pub(const char* t, float v, unsigned int decimals = 2) {
    char buf[16];
    dtostrf(v, 1, decimals, buf);
    pub(t, buf);
}

static void pub(const char* t, int32_t v)  { char b[16]; itoa(v, b, 10);  pub(t, b); }
static void pub(const char* t, uint32_t v) { char b[16]; utoa(v, b, 10);  pub(t, b); }
static void pub(const char* t, bool v)     { pub(t, v ? "1" : "0"); }

static void publishAll() {
    const VictronMpptData* devs = victronBleGetDevices();
    uint8_t n = victronBleGetDeviceCount();
    for (uint8_t i = 0; i < n; i++) {
        const VictronMpptData& d = devs[i];
        char sub[64];

        snprintf(sub, sizeof(sub), "solar/%d/name",              i); pub(tp(sub), d.name);
        snprintf(sub, sizeof(sub), "solar/%d/valid",             i); pub(tp(sub), (int32_t)d.valid);
        snprintf(sub, sizeof(sub), "solar/%d/pv_power_w",        i); pub(tp(sub), d.pvPower_W, 0);
        snprintf(sub, sizeof(sub), "solar/%d/battery_voltage_v", i); pub(tp(sub), d.batteryVoltage_V, 2);
        snprintf(sub, sizeof(sub), "solar/%d/battery_current_a", i); pub(tp(sub), d.batteryCurrent_A, 1);
        snprintf(sub, sizeof(sub), "solar/%d/yield_today_kwh",   i); pub(tp(sub), d.yieldToday_kWh, 2);
        snprintf(sub, sizeof(sub), "solar/%d/charger_state",     i); pub(tp(sub), (int32_t)d.chargerState);
        snprintf(sub, sizeof(sub), "solar/%d/error_code",        i); pub(tp(sub), (int32_t)d.errorCode);
        snprintf(sub, sizeof(sub), "solar/%d/rssi_dbm",          i); pub(tp(sub), (int32_t)d.rssi);
    }
    pub(tp("solar/total_pv_w"), victronBleGetTotalPvPower(), 0);

    pub(tp("wifi/rssi_dbm"),   (int32_t)wifiGetRssi());
    pub(tp("wifi/uptime_s"),   wifiGetUptime());
    pub(tp("wifi/ip"),         wifiGetIp());
    pub(tp("mcu/uptime_s"),    millis() / 1000UL);
    pub(tp("mcu/free_heap_b"), (uint32_t)ESP.getFreeHeap());
    pub(tp("status"),          "online", true);

    const VictronMpptData* vdevs = victronBleGetDevices();
    uint8_t vcount = victronBleGetDeviceCount();
    for (uint8_t i = 0; i < vcount; i++) {
        const VictronMpptData& v = vdevs[i];
        char prefix[48];
        snprintf(prefix, sizeof(prefix), "victron/%d", i);
        char subtopic[64];

        snprintf(subtopic, sizeof(subtopic), "%s/valid",   prefix); pub(tp(subtopic), v.valid);
        snprintf(subtopic, sizeof(subtopic), "%s/pv_w",    prefix); pub(tp(subtopic), v.pvPower_W, 0);
        snprintf(subtopic, sizeof(subtopic), "%s/bat_v",   prefix); pub(tp(subtopic), v.batteryVoltage_V, 2);
        snprintf(subtopic, sizeof(subtopic), "%s/bat_a",   prefix); pub(tp(subtopic), v.batteryCurrent_A, 1);
        snprintf(subtopic, sizeof(subtopic), "%s/yield_kwh",prefix);pub(tp(subtopic), v.yieldToday_kWh, 2);
        snprintf(subtopic, sizeof(subtopic), "%s/state",   prefix); pub(tp(subtopic), (int32_t)v.chargerState);
    }
    pub(tp("victron/total_pv_w"), victronBleGetTotalPvPower(), 0);

    s_publishCount++;
    s_lastPublishMs  = millis();
    s_publishPending = false;
    Serial.printf("[MQTT] Published #%lu\n", s_publishCount);
}

static void beginConnect() {
    const char* srv = configGetMqttServer();

    // Heap guard: TLS handshake needs ~48 KB free
    if (configGetMqttTlsEnabled() && ESP.getFreeHeap() < 48000) {
        Serial.printf("[MQTT] Insufficient heap for TLS (%lu bytes free), retry later\n",
                      ESP.getFreeHeap());
        s_state = MqttState::DISCONNECTED;
        s_lastConnectMs = millis();
        return;
    }

    if (configGetMqttTlsEnabled()) {
        // Tear down previous instance first to free memory before allocating new one
        if (s_wifiClientSecure) {
            delete s_wifiClientSecure;
            s_wifiClientSecure = nullptr;
        }
        Serial.printf("[MQTT] Free heap before TLS alloc: %lu\n", ESP.getFreeHeap());
        s_wifiClientSecure = new WiFiClientSecure();
        if (!s_wifiClientSecure) {
            Serial.println("[MQTT] Failed to allocate WiFiClientSecure");
            s_state = MqttState::DISCONNECTED;
            s_lastConnectMs = millis();
            return;
        }
        configGetMqttCaCert(s_caCert, sizeof(s_caCert));
        if (strlen(s_caCert) > 0) {
            s_wifiClientSecure->setCACert(s_caCert);
        } else {
            s_wifiClientSecure->setInsecure();
        }
        s_mqtt.setClient(*s_wifiClientSecure);
    } else {
        // Free the secure client if TLS was previously used
        if (s_wifiClientSecure) {
            delete s_wifiClientSecure;
            s_wifiClientSecure = nullptr;
        }
        s_mqtt.setClient(s_wifiClient);
    }

    s_mqtt.setServer(srv, configGetMqttPort());
    s_mqtt.setKeepAlive(60);
    s_mqtt.setSocketTimeout(10);   // TLS handshake needs more time

    const char* user = strlen(configGetMqttUsername()) > 0 ? configGetMqttUsername() : nullptr;
    const char* pass = strlen(configGetMqttPassword()) > 0 ? configGetMqttPassword() : nullptr;

    String lwt = topic("status");
    bool ok = s_mqtt.connect(CONFIG_MQTT_CLIENT_ID, user, pass,
                              lwt.c_str(), 0, true, "offline");
    if (ok) {
        s_state = MqttState::CONNECTED;
        s_publishPending = true;
        Serial.printf("[MQTT] Connected to %s  tls=%s\n", srv,
                      configGetMqttTlsEnabled() ? "YES" : "NO");
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

void mqttApplyConfig() {
    if (s_mqtt.connected()) s_mqtt.disconnect();
    if (s_wifiClientSecure) {
        delete s_wifiClientSecure;
        s_wifiClientSecure = nullptr;
    }
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
    Serial.printf("[MQTT] %s pub=%lu tls=%s\n", str[(int)s_state], s_publishCount,
                  configGetMqttTlsEnabled() ? "YES" : "NO");
}
