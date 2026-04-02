#include "config_store.h"
#include "config.h"
#include <EEPROM.h>
#include <string.h>
#include <Arduino.h>

#define EEPROM_SIZE (sizeof(ConfigData))
static ConfigData s_cfg;

static void applyDefaults() {
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.magic   = CFG_MAGIC;
    s_cfg.version = CFG_VERSION;

    strncpy(s_cfg.wifiProfiles[0].ssid,     CONFIG_WIFI_SSID,     WIFI_SSID_LEN - 1);
    strncpy(s_cfg.wifiProfiles[0].password, CONFIG_WIFI_PASSWORD, WIFI_PASS_LEN - 1);
    s_cfg.wifiProfileCount = 1;

    s_cfg.apEnabled = false;
    strncpy(s_cfg.apSsid,     CONFIG_AP_NAME,     sizeof(s_cfg.apSsid) - 1);
    strncpy(s_cfg.apPassword, CONFIG_AP_PASSWORD, sizeof(s_cfg.apPassword) - 1);

    strncpy(s_cfg.mqttServer, CONFIG_MQTT_SERVER, sizeof(s_cfg.mqttServer) - 1);
    strncpy(s_cfg.mqttTopic,  CONFIG_MQTT_TOPIC,  sizeof(s_cfg.mqttTopic) - 1);
    s_cfg.mqttPort     = 1883;
    s_cfg.mqttInterval = 30;
    s_cfg.mqttEnabled  = false;

    s_cfg.backlightPct   = CONFIG_BACKLIGHT_PCT;
    s_cfg.displayTimeout = 600;
    s_cfg.victronDeviceCount = 0;
    Serial.println("[CFG] Defaults applied");
}

void configInit() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, s_cfg);
    if (s_cfg.magic != CFG_MAGIC || s_cfg.version != CFG_VERSION) {
        Serial.printf("[CFG] EEPROM invalid (magic=0x%04X ver=%d) – wiping\n",
                      s_cfg.magic, s_cfg.version);
        applyDefaults();
        configSave();
    } else {
        for (uint8_t i = 0; i < MAX_WIFI_PROFILES; i++) {
            s_cfg.wifiProfiles[i].ssid[WIFI_SSID_LEN-1]     = 0;
            s_cfg.wifiProfiles[i].password[WIFI_PASS_LEN-1] = 0;
        }
        s_cfg.apSsid[sizeof(s_cfg.apSsid)-1]        = 0;
        s_cfg.apPassword[sizeof(s_cfg.apPassword)-1] = 0;
        s_cfg.mqttServer[sizeof(s_cfg.mqttServer)-1] = 0;
        s_cfg.mqttTopic[sizeof(s_cfg.mqttTopic)-1]   = 0;
        for (uint8_t i = 0; i < MAX_VICTRON_DEVICES; i++) {
            s_cfg.victronDevices[i].name[VICTRON_NAME_LEN-1]  = 0;
            s_cfg.victronDevices[i].mac[VICTRON_MAC_LEN-1]    = 0;
            s_cfg.victronDevices[i].aesKey[VICTRON_KEY_LEN-1] = 0;
        }
        Serial.println("[CFG] Loaded from EEPROM");
    }
}

void configSave() {
    s_cfg.magic   = CFG_MAGIC;
    s_cfg.version = CFG_VERSION;
    EEPROM.put(0, s_cfg);
    EEPROM.commit();
    Serial.println("[CFG] Saved to EEPROM");
}

void configPrint() {
    Serial.println("=== CONFIG ===");
    Serial.printf("  WiFi profiles: %d\n", s_cfg.wifiProfileCount);
    for (uint8_t i = 0; i < s_cfg.wifiProfileCount; i++)
        Serial.printf("    [%d] '%s'\n", i, s_cfg.wifiProfiles[i].ssid);
    Serial.printf("  AP: %s (%s)\n", s_cfg.apEnabled ? "ON" : "OFF", s_cfg.apSsid);
    Serial.printf("  Victron devices: %d\n", s_cfg.victronDeviceCount);
    for (uint8_t i = 0; i < s_cfg.victronDeviceCount; i++)
        Serial.printf("    [%d] '%s' MAC=%s enabled=%d\n", i,
                      s_cfg.victronDevices[i].name,
                      s_cfg.victronDevices[i].mac,
                      s_cfg.victronDevices[i].enabled);
    Serial.printf("  MQTT: %s server=%s topic=%s port=%d ivl=%ds\n",
                  s_cfg.mqttEnabled ? "ON" : "OFF",
                  s_cfg.mqttServer, s_cfg.mqttTopic,
                  s_cfg.mqttPort, s_cfg.mqttInterval);
    Serial.println("==============");
}

const ConfigData& configGet() { return s_cfg; }

uint8_t configGetWifiProfileCount() { return s_cfg.wifiProfileCount; }
const WifiProfile& configGetWifiProfile(uint8_t i) {
    static WifiProfile empty = {};
    return (i < MAX_WIFI_PROFILES) ? s_cfg.wifiProfiles[i] : empty;
}
void configSetWifiProfile(uint8_t idx, const char* ssid, const char* pass) {
    if (idx >= MAX_WIFI_PROFILES) return;
    strncpy(s_cfg.wifiProfiles[idx].ssid,     ssid ? ssid : "", WIFI_SSID_LEN - 1);
    strncpy(s_cfg.wifiProfiles[idx].password, pass ? pass : "", WIFI_PASS_LEN - 1);
    s_cfg.wifiProfiles[idx].ssid[WIFI_SSID_LEN-1]     = 0;
    s_cfg.wifiProfiles[idx].password[WIFI_PASS_LEN-1] = 0;
}
void configSetWifiProfileCount(uint8_t n) {
    s_cfg.wifiProfileCount = (n > MAX_WIFI_PROFILES) ? MAX_WIFI_PROFILES : n;
}
bool        configGetApEnabled()           { return s_cfg.apEnabled; }
void        configSetApEnabled(bool v)     { s_cfg.apEnabled = v; }
const char* configGetApSsid()              { return s_cfg.apSsid; }
void        configSetApSsid(const char* v) {
    strncpy(s_cfg.apSsid, v, sizeof(s_cfg.apSsid) - 1);
    s_cfg.apSsid[sizeof(s_cfg.apSsid)-1] = 0;
}
const char* configGetApPassword()              { return s_cfg.apPassword; }
void        configSetApPassword(const char* v) {
    strncpy(s_cfg.apPassword, v, sizeof(s_cfg.apPassword) - 1);
    s_cfg.apPassword[sizeof(s_cfg.apPassword)-1] = 0;
}

uint8_t configGetVictronCount()          { return s_cfg.victronDeviceCount; }
void    configSetVictronCount(uint8_t n) {
    s_cfg.victronDeviceCount = (n > MAX_VICTRON_DEVICES) ? MAX_VICTRON_DEVICES : n;
}
const VictronDeviceCfg& configGetVictronDevice(uint8_t idx) {
    static VictronDeviceCfg empty = {};
    return (idx < MAX_VICTRON_DEVICES) ? s_cfg.victronDevices[idx] : empty;
}
void configSetVictronDevice(uint8_t idx, const char* name, const char* mac,
                             const char* aesKey, bool enabled) {
    if (idx >= MAX_VICTRON_DEVICES) return;
    strncpy(s_cfg.victronDevices[idx].name,   name   ? name   : "", VICTRON_NAME_LEN - 1);
    strncpy(s_cfg.victronDevices[idx].mac,    mac    ? mac    : "", VICTRON_MAC_LEN  - 1);
    strncpy(s_cfg.victronDevices[idx].aesKey, aesKey ? aesKey : "", VICTRON_KEY_LEN  - 1);
    s_cfg.victronDevices[idx].name[VICTRON_NAME_LEN-1]  = 0;
    s_cfg.victronDevices[idx].mac[VICTRON_MAC_LEN-1]    = 0;
    s_cfg.victronDevices[idx].aesKey[VICTRON_KEY_LEN-1] = 0;
    s_cfg.victronDevices[idx].enabled = enabled;
}

const char* configGetMqttServer()          { return s_cfg.mqttServer; }
void configSetMqttServer(const char* v) {
    strncpy(s_cfg.mqttServer, v, sizeof(s_cfg.mqttServer)-1);
    s_cfg.mqttServer[sizeof(s_cfg.mqttServer)-1] = 0;
}
const char* configGetMqttTopic()           { return s_cfg.mqttTopic; }
void configSetMqttTopic(const char* v) {
    strncpy(s_cfg.mqttTopic, v, sizeof(s_cfg.mqttTopic)-1);
    s_cfg.mqttTopic[sizeof(s_cfg.mqttTopic)-1] = 0;
}
uint16_t configGetMqttPort()               { return s_cfg.mqttPort; }
void     configSetMqttPort(uint16_t v)     { s_cfg.mqttPort = (v == 0) ? 1883 : v; }
uint16_t configGetMqttInterval()           { return s_cfg.mqttInterval; }
void     configSetMqttInterval(uint16_t v) {
    s_cfg.mqttInterval = (v < 5) ? 5 : (v > 3600 ? 3600 : v);
}
bool configGetMqttEnabled()                { return s_cfg.mqttEnabled; }
void configSetMqttEnabled(bool v)          { s_cfg.mqttEnabled = v; }

uint8_t  configGetBacklight()              { return s_cfg.backlightPct; }
void     configSetBacklight(uint8_t v)     {
    s_cfg.backlightPct = (v < 10) ? 10 : (v > 100 ? 100 : v);
}
uint16_t configGetDisplayTimeout()             { return s_cfg.displayTimeout; }
void     configSetDisplayTimeout(uint16_t v)   {
    s_cfg.displayTimeout = (v < 5) ? 5 : (v > 360000 ? 360000 : v);
}
