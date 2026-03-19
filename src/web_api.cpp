#include "web_api.h"
#include "config_store.h"
#include "jbd_bms.h"
#include "battery_estimator.h"
#include "soc_limiter.h"
#include "mqtt_client.h"
#include "wifi_manager.h"
#include "battery_estimator.h"
#include <ArduinoJson.h>
#include "history.h"
#include "history_sd.h"
#include "display.h"
#include "lcd_driver.h"

static WebServer* s_srv = nullptr;  // back-pointer set in webApiRegisterRoutes()
static bool s_screenshotPending = false;


// ─── /api/status ─────────────────────────────────────────────────────────────

static void handleApiStatus() {
    String wifi_ip = wifiGetIp();   // store before JSON

    DynamicJsonDocument doc(1024);

    doc["uptime_s"]    = millis() / 1000UL;
    doc["free_heap_b"] = ESP.getFreeHeap();
    doc["wifi"]["rssi"]   = wifiGetRssi();
    doc["wifi"]["ip"]     = wifi_ip.c_str();
    doc["wifi"]["uptime"] = wifiGetUptime();

    doc["bms"]["connected"]  = bmsGetState() == BmsState::CONNECTED;
    doc["bms"]["data_valid"] = bmsIsDataValid();
    if (bmsIsDataValid()) {
        const BmsBasicInfo& b = bmsGetData().basic;
        doc["bms"]["voltage_v"]  = b.totalVoltage_V;
        doc["bms"]["current_a"]  = b.current_A;
        doc["bms"]["soc_pct"]    = b.stateOfCharge_pct;
        doc["bms"]["charge_fet"] = (bool)(b.fetStatus & 0x01);
        doc["bms"]["disch_fet"]  = (bool)(b.fetStatus & 0x02);
    }

    doc["mqtt"]["connected"]     = mqttIsConnected();
    doc["mqtt"]["publish_count"] = mqttGetPublishCount();

    doc["soc_limiter"]["enabled"]  = configGetSocLimitEnabled();
    doc["soc_limiter"]["limiting"] = socLimiterIsLimiting();

    String out;
    serializeJson(doc, out);
    s_srv->send(200, "application/json", out);
}

static void handleApiScreenshot() {
    lcdCaptureBegin();
    lv_obj_invalidate(lv_scr_act());

    uint32_t deadline = millis() + 3000;
    while (!lcdCaptureIsDone() && millis() < deadline) {
        lv_task_handler();
        delay(5);
    }

    if (millis() >= deadline) {
        s_srv->send(503, "text/plain", "Capture timeout");
        return;
    }

    s_srv->sendHeader("Location", "/screenshot.bmp");
    s_srv->send(302, "text/plain", "");
}


// ─── /api/config GET ─────────────────────────────────────────────────────────

static void handleApiConfigGet() {
    DynamicJsonDocument doc(2048);  // bigger for profiles
    
    // WiFi profiles
    JsonArray wifi_profiles = doc.createNestedArray("wifiProfiles");
    for (uint8_t i = 0; i < configGetWifiProfileCount(); i++) {
        JsonObject prof = wifi_profiles.createNestedObject();
        prof["ssid"] = configGetWifiProfile(i).ssid;
        prof["hasPass"] = strlen(configGetWifiProfile(i).password) > 0;
    }
    doc["wifiProfileCount"] = configGetWifiProfileCount();
    
    // Other settings (unchanged)
    doc["bmsDeviceName"] = configGetBmsDeviceName();
    doc["bmsPollInterval"] = configGetBmsPollInterval();
    doc["mqttEnabled"] = configGetMqttEnabled();
    doc["mqttServer"] = configGetMqttServer();
    doc["mqttPort"] = configGetMqttPort();
    doc["mqttTopic"] = configGetMqttTopic();
    doc["mqttInterval"] = configGetMqttInterval();
    doc["socLimitEnabled"] = configGetSocLimitEnabled();
    doc["socLimitMax"] = configGetMaxChargeSoc();
    doc["estimatorTau"] = configGetEstTau();
    doc["displayBacklight"] = configGetBacklight();
    doc["displayTimeout"] = configGetDisplayTimeout();
    doc["apEnabled"] = configGetApEnabled();
    doc["apSsid"] = configGetApSsid();
    
    String out;
    serializeJson(doc, out);
    s_srv->send(200, "application/json", out);
}

// ─── /api/config/save POST ───────────────────────────────────────────────────
static void handleApiConfigSave() {
    auto arg = [&](const char* n) -> String {
        return s_srv->hasArg(n) ? s_srv->arg(n) : String();
    };

    // Snapshot before save
    // For WiFi: only care about the profile we're currently using
    String currently_connected_ssid = wifiGetSsid();  // "" if not connected
    bool old_ap_enabled  = configGetApEnabled();
    String old_ap_ssid   = configGetApSsid();
    String old_ap_pass   = configGetApPassword();

    // Find the current profile's password before overwriting
    String old_connected_pass = "";
    for (uint8_t i = 0; i < configGetWifiProfileCount(); i++) {
        if (String(configGetWifiProfile(i).ssid) == currently_connected_ssid) {
            old_connected_pass = configGetWifiProfile(i).password;
            break;
        }
    }

    String old_mqtt_server = configGetMqttServer();
    uint16_t old_mqtt_port = configGetMqttPort();
    bool old_mqtt_en       = configGetMqttEnabled();

    // --- WiFi profiles ---
    uint8_t new_wifi_count = 0;
    for (uint8_t i = 0; i < MAX_WIFI_PROFILES; i++) {
        String ssid_key = "wifiSsid" + String(i);
        String pass_key = "wifiPass" + String(i);

        if (s_srv->hasArg(ssid_key.c_str()) && s_srv->arg(ssid_key).length() > 0) {
            String new_pass = s_srv->hasArg(pass_key.c_str()) ? s_srv->arg(pass_key) : "";
            if (new_pass.length() > 0) {
                configSetWifiProfile(i, s_srv->arg(ssid_key).c_str(), new_pass.c_str());
            } else {
                // Keep existing password
                configSetWifiProfile(i, s_srv->arg(ssid_key).c_str(),
                                     configGetWifiProfile(i).password);
            }
            new_wifi_count = i + 1;
            Serial.printf("[API] WiFi profile #%d saved: '%s' (pass %s)\n",
                          i, s_srv->arg(ssid_key).c_str(),
                          new_pass.length() > 0 ? "updated" : "unchanged");
        }
    }
    configSetWifiProfileCount(new_wifi_count);

    // --- AP ---
    bool new_ap_enabled = arg("apEnabled") == "1";
    configSetApEnabled(new_ap_enabled);
    if (s_srv->hasArg("apSsid"))     configSetApSsid(arg("apSsid").c_str());
    String new_ap_pass = arg("apPassword");
    if (new_ap_pass.length() > 0)   configSetApPassword(new_ap_pass.c_str());

    // --- BMS ---
    if (s_srv->hasArg("bmsDeviceName")) configSetBmsDeviceName(arg("bmsDeviceName").c_str());
    String poll_str = arg("bmsPollInterval");
    if (poll_str.length()) configSetBmsPollInterval(poll_str.toInt());

    // --- MQTT ---
    configSetMqttEnabled(arg("mqttEnabled") == "1");
    if (s_srv->hasArg("mqttServer"))  configSetMqttServer(arg("mqttServer").c_str());
    String mqtt_port = arg("mqttPort");
    if (mqtt_port.length()) configSetMqttPort(mqtt_port.toInt());
    if (s_srv->hasArg("mqttTopic"))   configSetMqttTopic(arg("mqttTopic").c_str());
    String mqtt_interval = arg("mqttInterval");
    if (mqtt_interval.length()) configSetMqttInterval(mqtt_interval.toInt());

    // --- SOC limiter ---
    socLimiterSetEnabled(arg("socLimitEnabled") == "1");
    String soc = arg("socLimitMax");
    if (soc.length()) configSetMaxChargeSoc((uint8_t)soc.toInt());

    // --- Estimator ---
    String tau_str = arg("estTau");
    if (tau_str.length()) {
        float tau = tau_str.toFloat();
        configSetEstTau(tau);
        batEstSetTau(tau);
    }

    // --- Display ---
    String dto = arg("displayTimeout");
    if (dto.length()) configSetDisplayTimeout((uint16_t)dto.toInt());

    configSave();
    // socLimiterApplyConfig();

    // ── WiFi change detection ─────────────────────────────────────────────
    // Find the new password for the currently connected SSID (if still in list)
    String new_connected_pass = "";
    bool connected_ssid_still_exists = false;
    for (uint8_t i = 0; i < configGetWifiProfileCount(); i++) {
        if (String(configGetWifiProfile(i).ssid) == currently_connected_ssid) {
            new_connected_pass = configGetWifiProfile(i).password;
            connected_ssid_still_exists = true;
            break;
        }
    }

    bool ap_changed = (old_ap_enabled != new_ap_enabled)
                   || (old_ap_ssid != configGetApSsid())
                   || (new_ap_pass.length() > 0 && new_ap_pass != old_ap_pass);

    bool current_connection_changed =
        !wifiIsConnected()                          // not connected → try connecting
        || !connected_ssid_still_exists             // our SSID was removed from profiles
        || (new_connected_pass != old_connected_pass); // our password changed

    bool wifi_changed = ap_changed || current_connection_changed;

    // ── Apply ─────────────────────────────────────────────────────────────
    if (wifi_changed) {
        Serial.printf("[API] WiFi change detected (ap=%s conn=%s) – applying\n",
                      ap_changed ? "yes" : "no",
                      current_connection_changed ? "yes" : "no");
        wifiApplyConfig();
    } else {
        Serial.printf("[API] WiFi unchanged for current connection '%s' – no reconnect\n",
                      currently_connected_ssid.c_str());
    }

    bool mqtt_changed = (old_mqtt_server != configGetMqttServer())
                     || (old_mqtt_port   != configGetMqttPort())
                     || (old_mqtt_en     != configGetMqttEnabled());
    if (mqtt_changed) mqttApplyConfig();

    displayApplyConfig();
    s_srv->send(200, "text/plain", "OK");
    Serial.printf("[API] Config saved  wifi=%s  mqtt=%s\n",
                  wifi_changed ? "CHANGED" : "unchanged",
                  mqtt_changed ? "CHANGED" : "unchanged");
}


// ─── /api/mqtt/publish POST ──────────────────────────────────────────────────

static void handleApiMqttPublish() {
    mqttPublishNow();
    s_srv->send(200, "text/plain", "OK");
}

// ─── /api/now ────────────────────────────────────────────────────────────────

static void handleApiNow() {
    char buf[32];
    snprintf(buf, sizeof(buf), "{\"now\":%lu}", historyNow());
    s_srv->send(200, "application/json", buf);
}


static String uptimeString() {
    uint32_t sec  = millis() / 1000;
    uint32_t days = sec / 86400; sec %= 86400;
    uint32_t h    = sec / 3600;  sec %= 3600;
    uint32_t m    = sec / 60;    sec %= 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu", days, h, m, sec);
    return String(buf);
}

static String formatTime(uint32_t secs) {
    char buf[24];
    batEstFormatTime(secs, buf, sizeof(buf));
    return String(buf);
}

static const char* socLimiterStateStr() {
    switch (socLimiterGetState()) {
        case SocLimiterState::LIMITER_DISABLED: return "Disabled";
        case SocLimiterState::MONITORING:       return "Monitoring";
        case SocLimiterState::LIMITING:         return "Limiting (FET OFF)";
        case SocLimiterState::WAITING_FOR_BMS:  return "Waiting for BMS";
        default:                                return "Unknown";
    }
}

static const char* wifiStateStr() {
    switch (wifiGetState()) {
        case WifiState::IDLE:          return "Idle";
        case WifiState::CONNECTING:    return "Connecting";
        case WifiState::CONNECTED:     return "Connected";
        case WifiState::WAITING_RETRY: return "Waiting to retry";
        case WifiState::AP:            return "AP";
        default:                       return "Unknown";
    }
}


static void handleApiData() {
    StaticJsonDocument<2048> doc;

    const BmsData& bms = bmsGetData();
    JsonObject jBms    = doc.createNestedObject("bms");
    jBms["connected"]  = (bmsGetState() == BmsState::CONNECTED);
    jBms["dataValid"]  = bmsIsDataValid();

    if (bmsIsDataValid()) {
        const BmsBasicInfo& b = bms.basic;
        jBms["voltage"]      = serialized(String(b.totalVoltage_V,     2));
        jBms["current"]      = serialized(String(b.current_A,          2));
        jBms["soc"]          = b.stateOfCharge_pct;
        jBms["remainCap"]    = serialized(String(b.remainCapacity_Ah,  2));
        jBms["nominalCap"]   = serialized(String(b.nominalCapacity_Ah, 2));
        jBms["cycles"]       = b.cycleCount;
        jBms["chargeFet"]    = (bool)(b.fetStatus & 0x01);
        jBms["dischargeFet"] = (bool)(b.fetStatus & 0x02);
        jBms["protection"]   = b.protectionStatus;

        JsonArray jTemps = jBms.createNestedArray("temps");
        for (uint8_t i = 0; i < b.numNTC && i < 8; i++)
            jTemps.add(serialized(String(b.temperature_C[i], 1)));

        JsonArray jCells = jBms.createNestedArray("cells");
        if (bms.cells.valid)
            for (uint8_t i = 0; i < bms.cells.cellCount; i++)
                jCells.add(bms.cells.cellVoltage_mV[i]);
    }

    JsonObject jEst = doc.createNestedObject("estimator");
    jEst["valid"]   = batEstIsValid();
    if (batEstIsValid()) {
        const BatEstimate& e    = batEstGet();
        jEst["filteredCurrent"] = serialized(String(e.filteredCurrent_A,  3));
        jEst["power"]           = serialized(String(e.power_W,            1));
        jEst["remainEnergy"]    = serialized(String(e.remainingEnergy_Wh, 1));
        jEst["energyToFull"]    = serialized(String(e.energyToFull_Wh,    1));
        jEst["dischargeTimeValid"] = e.dischargeTimeValid;
        jEst["dischargeTimeSec"]   = e.remainingDischargeTime_s;
        jEst["dischargeTimeStr"]   = formatTime(e.remainingDischargeTime_s);
        jEst["chargeTimeValid"]    = e.chargeTimeValid;
        jEst["chargeTimeSec"]      = e.remainingChargeTime_s;
        jEst["chargeTimeStr"]      = formatTime(e.remainingChargeTime_s);
    }

    JsonObject jLim  = doc.createNestedObject("socLimiter");
    jLim["enabled"]  = configGetSocLimitEnabled();
    jLim["limiting"] = socLimiterIsLimiting();
    jLim["state"]    = socLimiterStateStr();
    jLim["threshold"]= socLimiterGetThreshold();
    jLim["resumeAt"] = socLimiterGetResumeAt();

    // Store String temporaries BEFORE building JSON — prevents dangling pointers
    String wifi_ssid   = wifiGetSsid();
    String wifi_ip     = wifiGetIp();
    String wifi_ap_ip  = wifiGetApIp();
    String uptime_str  = uptimeString();

    JsonObject jWifi = doc.createNestedObject("wifi");
    jWifi["state"]     = wifiStateStr();
    jWifi["connected"] = wifiIsConnected();
    jWifi["ip"]        = wifi_ip.c_str();
    jWifi["ssid"]      = wifi_ssid.c_str();
    jWifi["rssi"]      = wifiGetRssi();
    jWifi["uptime"]    = wifiGetUptime();
    jWifi["apEnabled"] = configGetApEnabled();
    jWifi["apSsid"]    = configGetApSsid();      // const char* from EEPROM — safe
    jWifi["apIp"]      = wifi_ap_ip.c_str();
    jWifi["mode"]      = wifiIsAp() ? "AP" : "STA";

    JsonObject jMcu = doc.createNestedObject("mcu");
    jMcu["uptime"]     = uptime_str.c_str();
    jMcu["freeHeap"]   = ESP.getFreeHeap();
    jMcu["heapSize"]   = ESP.getHeapSize();
    jMcu["cpuFreqMHz"] = ESP.getCpuFreqMHz();
    jMcu["flashSize"]  = ESP.getFlashChipSize();
    jMcu["sdkVersion"] = ESP.getSdkVersion();

    String json;
    serializeJson(doc, json);
    s_srv->send(200, "application/json", json);
}

static void handleApiFet() {
    bool changed = false;

    if (s_srv->hasArg("charge")) {
        bool on = s_srv->arg("charge") == "1";
        socLimiterManualFetOverride();  // suspend limiter if active
        bmsSetChargeFet(on);
        changed = true;
    }
    if (s_srv->hasArg("discharge")) {
        bool on = s_srv->arg("discharge") == "1";
        bmsSetDischargeFet(on);
        changed = true;
    }
    if (!changed) {
        s_srv->send(400, "text/plain", "supply charge= and/or discharge=");
        return;
    }

    StaticJsonDocument<64> doc;
    if (bmsIsDataValid()) {
        doc["chargeFet"]    = (bool)(bmsGetData().basic.fetStatus & 0x01);
        doc["dischargeFet"] = (bool)(bmsGetData().basic.fetStatus & 0x02);
    } else {
        doc["chargeFet"]    = nullptr;
        doc["dischargeFet"] = nullptr;
    }
    String json;
    serializeJson(doc, json);
    s_srv->send(200, "application/json", json);
}


static void handleApiHistory() {
    uint32_t nowSec  = historyNow();
    uint32_t toSec   = nowSec;
    uint32_t fromSec = (nowSec > 300) ? nowSec - 300 : 0;

    if (s_srv->hasArg("from")) {
        int32_t v = (int32_t)s_srv->arg("from").toInt();
        fromSec = (v < 0) ? 0 : (uint32_t)v;
    }
    if (s_srv->hasArg("to")) {
        int32_t v = (int32_t)s_srv->arg("to").toInt();
        toSec = (v < 0) ? 0 : (uint32_t)v;
    }
    if (toSec <= fromSec) toSec = fromSec + 1;

    uint32_t window = toSec - fromSec;
    uint8_t  tier   = 2;
    if      (window <= (uint32_t)(HISTORY_T0_CAPACITY * HISTORY_T0_INTERVAL_S)) tier = 0;
    else if (window <= (uint32_t)(HISTORY_T1_CAPACITY * HISTORY_T1_INTERVAL_S)) tier = 1;

    const RingBuf_t* r = historyGetRingBuf(tier);
    if (!r) { s_srv->send(500, "text/plain", "bad tier"); return; }

    Serial.printf("[API] /api/history from=%lu to=%lu window=%lus tier=%u count=%u\n",
        fromSec, toSec, window, tier, r->count);

    s_srv->setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_srv->send(200, "application/json", "");

    char hdr[160];
    snprintf(hdr, sizeof(hdr),
        "{\"tier\":%u,\"interval\":%lu,\"from\":%lu,"
        "\"to\":%lu,\"now\":%lu,\"points\":[",
        tier, historyGetInterval(tier), fromSec, toSec, nowSec);
    s_srv->sendContent(hdr);

    bool firstPoint = true;
    for (uint16_t i = 0; i < r->count; i++) {
        uint32_t raw = (uint32_t)r->head + r->capacity - r->count + i;
        const HistoryPoint& pt = r->buf[raw % r->capacity];
        if (pt.ts < fromSec || pt.ts > toSec) continue;

        char buf[112];
        snprintf(buf, sizeof(buf),
            "%s{\"t\":%lu,\"s\":%u,\"c\":%d,\"v\":[%u,%u,%u,%u],\"u\":%u}",
            firstPoint ? "" : ",",
            pt.ts, pt.soc, (int)pt.current_cA,
            pt.cell_mV[0], pt.cell_mV[1], pt.cell_mV[2], pt.cell_mV[3],
            pt.voltage_mV);
        s_srv->sendContent(buf);
        firstPoint = false;
    }

    s_srv->sendContent("]}");
    s_srv->sendContent("");
}

// ─── Registration ─────────────────────────────────────────────────────────────

void webApiRegisterRoutes(WebServer& server) {
    s_srv = &server;
    server.on("/api/now",          HTTP_GET,  handleApiNow);
    server.on("/api/data",         HTTP_GET,  handleApiData);
    server.on("/api/history",      HTTP_GET,  handleApiHistory);
    server.on("/api/fet",          HTTP_POST, handleApiFet);
    server.on("/api/status",       HTTP_GET,  handleApiStatus);
    server.on("/api/config",       HTTP_GET,  handleApiConfigGet);
    server.on("/api/config/save",  HTTP_POST, handleApiConfigSave);
    server.on("/api/mqtt/publish", HTTP_POST, handleApiMqttPublish);
    server.on("/api/screenshot",   HTTP_GET,  handleApiScreenshot);
}