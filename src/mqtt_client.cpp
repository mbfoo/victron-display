#include "mqtt_client.h"
#include "config_store.h"
#include "wifi_manager.h"
#include "jbd_bms.h"
#include "battery_estimator.h"
#include "soc_limiter.h"
#include "watchdog.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ─── Internal objects ─────────────────────────────────────────────────────────
static WiFiClient    s_wifiClient;
static PubSubClient  s_mqtt(s_wifiClient);

// ─── Internal state ───────────────────────────────────────────────────────────
static MqttState s_state           = MqttState::WAITING_FOR_WIFI;
static uint32_t  s_lastConnectMs   = 0;
static uint32_t  s_lastPublishMs   = 0;
static uint32_t  s_publishCount    = 0;
static bool      s_publishPending  = false;

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Build a full topic path: <base>/<subtopic>
static String topic(const String& subtopic) {
    String t = String(configGetMqttTopic());
    if (!t.endsWith("/")) t += "/";
    t += subtopic;
    return t;
}

static bool serverConfigured() {
    const char* srv = configGetMqttServer();
    return srv != nullptr && strlen(srv) > 0 && strcmp(srv, "0.0.0.0") != 0;
}

// ─── Publish helpers ──────────────────────────────────────────────────────────

static void pub(const String& t, const String& payload, bool retain = false) {
    if (!s_mqtt.connected()) return;
    if (!s_mqtt.publish(t.c_str(), payload.c_str(), retain)) {
        Serial.printf("[MQTT] Publish failed: %s\n", t.c_str());
    }
}

static void pub(const String& t, float v, unsigned int decimals = 2) {
    pub(t, String(v, decimals));
}
static void pub(const String& t, int32_t v)  { pub(t, String(v));   }
static void pub(const String& t, uint32_t v) { pub(t, String(v));   }
static void pub(const String& t, bool v)     { pub(t, String(v ? "1" : "0")); }

// ─── Publish all data ─────────────────────────────────────────────────────────

static void publishAll() {
    Serial.printf("[MQTT] Publishing to \"%s/...\"\n", configGetMqttTopic());

    // ── BMS basic ─────────────────────────────────────────────────────────────
    pub(topic("bms/connected"),    bmsGetState() == BmsState::CONNECTED);
    pub(topic("bms/data_valid"),   bmsIsDataValid());

    if (bmsIsDataValid()) {
        const BmsBasicInfo& b = bmsGetData().basic;
        pub(topic("bms/voltage"),          b.totalVoltage_V,        2);
        pub(topic("bms/current"),          b.current_A,             2);
        pub(topic("bms/soc"),              (int32_t)b.stateOfCharge_pct);
        pub(topic("bms/remain_cap_ah"),    b.remainCapacity_Ah,     2);
        pub(topic("bms/nominal_cap_ah"),   b.nominalCapacity_Ah,    2);
        pub(topic("bms/cycle_count"),      (int32_t)b.cycleCount);
        pub(topic("bms/charge_fet"),       (bool)(b.fetStatus & 0x01));
        pub(topic("bms/discharge_fet"),    (bool)(b.fetStatus & 0x02));
        pub(topic("bms/protection_flags"), (int32_t)b.protectionStatus);

        for (uint8_t i = 0; i < b.numNTC && i < 8; i++) {
            String tpc = "bms/temp_" + String(i) + "_c";
            pub(topic(tpc), b.temperature_C[i], 1);
        }


        // ── Cell voltages ─────────────────────────────────────────────────────
        const BmsCellData& c = bmsGetData().cells;
        if (c.valid) {
            uint16_t minV = 65535, maxV = 0;
            uint32_t sumV = 0;
            for (uint8_t i = 0; i < c.cellCount; i++) {
                String tpc = "bms/cell_" + String(i + 1) + "_mv";
                pub(topic(tpc), (int32_t)c.cellVoltage_mV[i]);
                if (c.cellVoltage_mV[i] < minV) minV = c.cellVoltage_mV[i];
                if (c.cellVoltage_mV[i] > maxV) maxV = c.cellVoltage_mV[i];
                sumV += c.cellVoltage_mV[i];
            }
            pub(topic("bms/cell_min_mv"),   (int32_t)minV);
            pub(topic("bms/cell_max_mv"),   (int32_t)maxV);
            pub(topic("bms/cell_delta_mv"), (int32_t)(maxV - minV));
            pub(topic("bms/cell_avg_mv"),   (int32_t)(sumV / c.cellCount));
        }

    }

    // ── Estimator ─────────────────────────────────────────────────────────────
    if (batEstIsValid()) {
        const BatEstimate& e = batEstGet();
        pub(topic("estimator/filtered_current_a"),   e.filteredCurrent_A,          3);
        pub(topic("estimator/power_w"),              e.power_W,                    1);
        pub(topic("estimator/remaining_energy_wh"),  e.remainingEnergy_Wh,         1);
        pub(topic("estimator/energy_to_full_wh"),    e.energyToFull_Wh,            1);

        if (e.dischargeTimeValid)
            pub(topic("estimator/discharge_time_s"), e.remainingDischargeTime_s);
        if (e.chargeTimeValid)
            pub(topic("estimator/charge_time_s"),    e.remainingChargeTime_s);
    }

    // ── SOC limiter ───────────────────────────────────────────────────────────
    pub(topic("soc_limiter/enabled"),   configGetSocLimitEnabled());
    pub(topic("soc_limiter/limiting"),  socLimiterIsLimiting());
    pub(topic("soc_limiter/threshold"), (int32_t)socLimiterGetThreshold());
    pub(topic("soc_limiter/resume_at"), (int32_t)socLimiterGetResumeAt());

    // ── WiFi ──────────────────────────────────────────────────────────────────
    pub(topic("wifi/rssi_dbm"),         (int32_t)wifiGetRssi());
    pub(topic("wifi/uptime_s"),         wifiGetUptime());
    pub(topic("wifi/ip"),               wifiGetIp());

    // ── MCU ───────────────────────────────────────────────────────────────────
    pub(topic("mcu/uptime_s"),          millis() / 1000UL);
    pub(topic("mcu/free_heap_b"),       (uint32_t)ESP.getFreeHeap());
    pub(topic("mcu/bms_disconn_s"),     watchdogGetBmsDisconnectedMs() / 1000UL);

    // ── Last-will / online flag (retained so broker knows device state) ───────
    pub(topic("status"), "online", true /*retain*/);

    s_publishCount++;
    s_lastPublishMs  = millis();
    s_publishPending = false;

    Serial.printf("[MQTT] Published  count=%lu\n", s_publishCount);
}

// ─── Connect ──────────────────────────────────────────────────────────────────

static void beginConnect() {
    const char* server = configGetMqttServer();
    Serial.printf("[MQTT] Connecting to %s:%d  id=\"%s\"\n",
                  server, configGetMqttPort(), MQTT_CLIENT_ID);

    s_mqtt.setServer(server, configGetMqttPort());
    s_mqtt.setKeepAlive(60);
    s_mqtt.setSocketTimeout(5);    // non-blocking: give up TCP after 5 s

    // Last-will: publish "offline" to status topic if we disconnect ungracefully
    String statusTopic = topic("status");
    bool ok = s_mqtt.connect(
        MQTT_CLIENT_ID,
        nullptr,                       // no username
        nullptr,                       // no password
        statusTopic.c_str(),           // LWT topic
        0,                             // LWT QoS
        true,                          // LWT retain
        "offline"                      // LWT message
    );

    if (ok) {
        Serial.printf("[MQTT] Connected to %s\n", server);
        s_state = MqttState::CONNECTED;
        // Publish immediately after connect
        s_publishPending = true;
    } else {
        Serial.printf("[MQTT] Connect failed  rc=%d  retry in %lus\n",
                      s_mqtt.state(), MQTT_CONNECT_RETRY_MS / 1000UL);
        s_state        = MqttState::DISCONNECTED;
        s_lastConnectMs = millis();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public: init / task
// ═══════════════════════════════════════════════════════════════════════════════

void mqttInit() {
    s_publishCount   = 0;
    s_lastPublishMs  = 0;
    s_publishPending = false;
    s_lastConnectMs  = 0;

    if (!configGetMqttEnabled() || !serverConfigured()) {
        Serial.println("[MQTT] No server configured – disabled");
        s_state = MqttState::MQTT_DISABLED;
        return;
    }

    s_state = MqttState::WAITING_FOR_WIFI;
    Serial.printf("[MQTT] Init  server=%s  topic=%s  interval=%lus\n",
                  configGetMqttServer(),
                  configGetMqttTopic(),
                  (uint32_t)configGetMqttInterval() * 1000UL / 1000UL);
}

void mqttTask() {
    uint32_t now = millis();

    switch (s_state) {

    case MqttState::MQTT_DISABLED:
        return;

    // ── Wait for WiFi before even trying ──────────────────────────────────────
    case MqttState::WAITING_FOR_WIFI:
        if (wifiIsConnected()) {
            s_state = MqttState::CONNECTING;
            beginConnect();
        }
        return;

    // ── Connecting: beginConnect() is synchronous but has a 5s socket timeout─
    case MqttState::CONNECTING:
        // beginConnect() already transitions state — nothing to poll here
        return;

    // ── Connected ─────────────────────────────────────────────────────────────
    case MqttState::CONNECTED:
        // Must call loop() to service the MQTT keep-alive
        s_mqtt.loop();

        if (!s_mqtt.connected()) {
            Serial.printf("[MQTT] Connection lost  rc=%d\n", s_mqtt.state());
            s_state         = MqttState::DISCONNECTED;
            s_lastConnectMs = now;
            return;
        }

        // Drop back to waiting if WiFi is gone
        if (!wifiIsConnected()) {
            Serial.println("[MQTT] WiFi lost – waiting for reconnect");
            s_mqtt.disconnect();
            s_state = MqttState::WAITING_FOR_WIFI;
            return;
        }

        // Periodic publish or forced publish
        if (s_publishPending ||
            (now - s_lastPublishMs >= (uint32_t)configGetMqttInterval() * 1000UL)) {
            publishAll();
        }
        return;

    // ── Disconnected: wait, then retry ────────────────────────────────────────
    case MqttState::DISCONNECTED:
        if (!wifiIsConnected()) {
            s_state = MqttState::WAITING_FOR_WIFI;
            return;
        }
        if (now - s_lastConnectMs >= MQTT_CONNECT_RETRY_MS) {
            s_state = MqttState::CONNECTING;
            beginConnect();
        }
        return;
    }
}


// ═══════════════════════════════════════════════════════════════════════════════
// Public: API
// ═══════════════════════════════════════════════════════════════════════════════
void mqttApplyConfig() {
    if (s_mqtt.connected()) s_mqtt.disconnect();
    if (!configGetMqttEnabled() || !serverConfigured()) {
        s_state = MqttState::MQTT_DISABLED;
        Serial.println("[MQTT] Disabled by config apply");
    } else {
        s_state       = MqttState::WAITING_FOR_WIFI;
        s_lastConnectMs = 0;
        Serial.printf("[MQTT] Config applied – reconnecting to %s:%d every %ds\n",
            configGetMqttServer(), configGetMqttPort(), configGetMqttInterval());
    }
}

MqttState mqttGetState()          { return s_state; }
bool      mqttIsConnected()       { return s_state == MqttState::CONNECTED && s_mqtt.connected(); }
uint32_t  mqttGetPublishCount()   { return s_publishCount; }
uint32_t  mqttGetLastPublishMs()  { return s_lastPublishMs; }

void mqttPublishNow() {
    if (s_state == MqttState::CONNECTED) {
        s_publishPending = true;
        Serial.println("[MQTT] Immediate publish requested");
    }
}

void mqttPrint() {
    const char* stateStr[] = {
        "DISABLED", "WAITING_FOR_WIFI", "CONNECTING", "CONNECTED", "DISCONNECTED"
    };
    Serial.println("\n========= MQTT =========");
    Serial.printf("  State         : %s\n", stateStr[(int)s_state]);
    Serial.printf("  Server        : %s:%d\n", configGetMqttServer(), configGetMqttPort());
    Serial.printf("  Topic base    : %s\n",  configGetMqttTopic());
    Serial.printf("  Connected     : %s\n",  mqttIsConnected() ? "YES" : "NO");
    Serial.printf("  Publish count : %lu\n", s_publishCount);
    if (s_lastPublishMs > 0)
        Serial.printf("  Last publish  : %lu s ago\n", (millis() - s_lastPublishMs) / 1000UL);
    Serial.println("========================\n");
}
