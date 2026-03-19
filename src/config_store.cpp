#include "config_store.h"
#include "config.h"
#include "EEPROM.h"

#define EEPROM_SIZE (sizeof(ConfigData))

static ConfigData s_cfg;

static void applyDefaults() {
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.magic = CONFIG_MAGIC;
    s_cfg.version = CONFIG_VERSION;
    
    // WiFi profiles from config.h
    configSetWifiProfile(0, CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
    s_cfg.wifiProfileCount = 2;  // adjust based on your defaults
    
    // Other defaults (unchanged)
    strncpy(s_cfg.bmsDeviceName, CONFIG_BMS_BLE_NAME, sizeof(s_cfg.bmsDeviceName) - 1);
    strncpy(s_cfg.mqttServer, CONFIG_MQTT_SERVER, sizeof(s_cfg.mqttServer) - 1);
    strncpy(s_cfg.mqttTopic, CONFIG_MQTT_TOPIC, sizeof(s_cfg.mqttTopic) - 1);
    s_cfg.maxChargeSoc = CONFIG_MAX_CHARGE_SOC;
    s_cfg.socLimitEnabled = CONFIG_SOC_LIMIT_ENABLED;
    
    s_cfg.apEnabled = false;
    strncpy(s_cfg.apSsid, CONFIG_AP_NAME, sizeof(s_cfg.apSsid) - 1);
    strncpy(s_cfg.apPassword, CONFIG_AP_PASSWORD, sizeof(s_cfg.apPassword) - 1);
    
    s_cfg.backlightPct = 100;
    s_cfg.estTauSeconds = 60.0f;
    s_cfg.bmsPollInterval = 5000;
    s_cfg.mqttEnabled = true;
    s_cfg.mqttPort = 1883;
    s_cfg.mqttInterval = 30;
    s_cfg.displayTimeout = 600;
    
    Serial.println("CFG: Defaults applied from config.h");
}

void configInit() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, s_cfg);
    
    if (s_cfg.magic != CONFIG_MAGIC || s_cfg.version != CONFIG_VERSION) {
        Serial.printf("CFG: EEPROM invalid (magic=0x%04X ver=%d) – wiping\n",
                      s_cfg.magic, s_cfg.version);
        applyDefaults();
        configSave();
    } else {
        // Null-terminate strings
        s_cfg.bmsDeviceName[sizeof(s_cfg.bmsDeviceName)-1] = 0;
        s_cfg.mqttServer[sizeof(s_cfg.mqttServer)-1] = 0;
        s_cfg.mqttTopic[sizeof(s_cfg.mqttTopic)-1] = 0;
        s_cfg.apSsid[sizeof(s_cfg.apSsid)-1] = 0;
        s_cfg.apPassword[sizeof(s_cfg.apPassword)-1] = 0;
        
        for (uint8_t i = 0; i < MAX_WIFI_PROFILES; i++) {
            s_cfg.wifiProfiles[i].ssid[WIFI_SSID_LEN-1] = 0;
            s_cfg.wifiProfiles[i].password[WIFI_PASS_LEN-1] = 0;
        }
        Serial.println("CFG: Loaded from EEPROM");
    }
}

void configSave() {
    s_cfg.magic = CONFIG_MAGIC;
    s_cfg.version = CONFIG_VERSION;
    EEPROM.put(0, s_cfg);
    EEPROM.commit();
    Serial.println("CFG: Saved to EEPROM");
}

void configPrint() {
    Serial.println("=== CONFIG ===");
    Serial.printf("WiFi Profiles: %d\n", s_cfg.wifiProfileCount);
    for (uint8_t i = 0; i < s_cfg.wifiProfileCount; i++) {
        Serial.printf("  [%d] SSID: '%s'  Pass: '%s'\n", i,
                      s_cfg.wifiProfiles[i].ssid,
                      strlen(s_cfg.wifiProfiles[i].password) ? "***" : "empty");
    }
    Serial.printf("BMS Name: '%s'\n", s_cfg.bmsDeviceName);
    Serial.printf("MQTT Server: '%s'  Topic: '%s'\n", s_cfg.mqttServer, s_cfg.mqttTopic);
    Serial.printf("Max SOC: %d%%  Limit: %s\n", s_cfg.maxChargeSoc, s_cfg.socLimitEnabled ? "ON" : "OFF");
    Serial.printf("AP Enabled: %s  SSID: '%s'\n", s_cfg.apEnabled ? "YES" : "NO", s_cfg.apSsid);
    Serial.printf("Backlight: %d%%\n", s_cfg.backlightPct);
    Serial.printf("BMS Poll: %d ms\n", s_cfg.bmsPollInterval);
    Serial.printf("MQTT En: %s  Port: %d  Ivl: %d s\n", s_cfg.mqttEnabled ? "YES" : "NO", s_cfg.mqttPort, s_cfg.mqttInterval);
    Serial.printf("Display off: %d s\n", s_cfg.displayTimeout);
    Serial.println("==============");
}

// Getters
const ConfigData& configGet() { return s_cfg; }

uint8_t configGetWifiProfileCount() { return s_cfg.wifiProfileCount; }
const WifiProfile& configGetWifiProfile(uint8_t idx) {
    static WifiProfile empty = {};
    return (idx < s_cfg.wifiProfileCount) ? s_cfg.wifiProfiles[idx] : empty;
}

const char* configGetBmsDeviceName() { return s_cfg.bmsDeviceName; }
const char* configGetMqttServer() { return s_cfg.mqttServer; }
const char* configGetMqttTopic() { return s_cfg.mqttTopic; }
uint8_t configGetMaxChargeSoc() { return s_cfg.maxChargeSoc; }
bool configGetSocLimitEnabled() { return s_cfg.socLimitEnabled; }
bool configGetApEnabled() { return s_cfg.apEnabled; }
const char* configGetApSsid() { return s_cfg.apSsid; }
const char* configGetApPassword() { return s_cfg.apPassword; }
uint8_t configGetBacklight() { return s_cfg.backlightPct; }
float configGetEstTau() { return s_cfg.estTauSeconds; }
uint16_t configGetBmsPollInterval() { return s_cfg.bmsPollInterval; }
bool configGetMqttEnabled() { return s_cfg.mqttEnabled; }
uint16_t configGetMqttPort() { return s_cfg.mqttPort; }
uint16_t configGetMqttInterval() { return s_cfg.mqttInterval; }
uint16_t configGetDisplayTimeout() { return s_cfg.displayTimeout; }

// Setters
void configSetWifiProfile(uint8_t idx, const char* ssid, const char* pass) {
    if (idx >= MAX_WIFI_PROFILES) return;
    strncpy(s_cfg.wifiProfiles[idx].ssid, ssid ? ssid : "", WIFI_SSID_LEN - 1);
    strncpy(s_cfg.wifiProfiles[idx].password, pass ? pass : "", WIFI_PASS_LEN - 1);
    s_cfg.wifiProfiles[idx].ssid[WIFI_SSID_LEN-1] = 0;
    s_cfg.wifiProfiles[idx].password[WIFI_PASS_LEN-1] = 0;
}

void configSetWifiProfileCount(uint8_t count) {
    s_cfg.wifiProfileCount = (count > MAX_WIFI_PROFILES) ? MAX_WIFI_PROFILES : count;
}

void configSetBmsDeviceName(const char* v) {
    strncpy(s_cfg.bmsDeviceName, v, sizeof(s_cfg.bmsDeviceName) - 1);
    s_cfg.bmsDeviceName[sizeof(s_cfg.bmsDeviceName)-1] = 0;
}

// ... other setters unchanged
void configSetMqttServer(const char* v) {
    strncpy(s_cfg.mqttServer, v, sizeof(s_cfg.mqttServer) - 1);
    s_cfg.mqttServer[sizeof(s_cfg.mqttServer)-1] = 0;
}
void configSetMqttTopic(const char* v) {
    strncpy(s_cfg.mqttTopic, v, sizeof(s_cfg.mqttTopic) - 1);
    s_cfg.mqttTopic[sizeof(s_cfg.mqttTopic)-1] = 0;
}
void configSetMaxChargeSoc(uint8_t v) { s_cfg.maxChargeSoc = v; }
void configSetSocLimitEnabled(bool v) { s_cfg.socLimitEnabled = v; }
void configSetApEnabled(bool v) { s_cfg.apEnabled = v; }
void configSetApSsid(const char* v) {
    strncpy(s_cfg.apSsid, v, sizeof(s_cfg.apSsid) - 1);
    s_cfg.apSsid[sizeof(s_cfg.apSsid)-1] = 0;
}
void configSetApPassword(const char* v) {
    strncpy(s_cfg.apPassword, v, sizeof(s_cfg.apPassword) - 1);
    s_cfg.apPassword[sizeof(s_cfg.apPassword)-1] = 0;
}
void configSetBacklight(uint8_t v) { 
    s_cfg.backlightPct = (v < 10) ? 10 : (v > 100 ? 100 : v); 
}
void configSetEstTau(float t) { 
    if (t < 1.0f) t = 600.0f;
    s_cfg.estTauSeconds = t; 
}
void configSetBmsPollInterval(uint16_t v) { 
    s_cfg.bmsPollInterval = (v < 500) ? 500 : v; 
}
void configSetMqttEnabled(bool v) { s_cfg.mqttEnabled = v; }
void configSetMqttPort(uint16_t v) { s_cfg.mqttPort = (v == 0) ? 1883 : v; }
void configSetMqttInterval(uint16_t v) { 
    s_cfg.mqttInterval = (v < 5) ? 5 : (v > 3600 ? 3600 : v); 
}
void configSetDisplayTimeout(uint16_t v) { 
    s_cfg.displayTimeout = (v < 5) ? 5 : (v > 360000 ? 360000 : v); 
}
