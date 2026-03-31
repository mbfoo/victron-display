#pragma once
#include <stdint.h>
#include <stdbool.h>

#define CFG_MAGIC        0xCAFE
#define CFG_VERSION      2   // bumped for MQTT TLS fields

#define MAX_WIFI_PROFILES   10
#define WIFI_SSID_LEN       33
#define WIFI_PASS_LEN       65

#define MAX_VICTRON_DEVICES 5
#define VICTRON_NAME_LEN    32
#define VICTRON_MAC_LEN     18   // "AA:BB:CC:DD:EE:FF\0"
#define VICTRON_KEY_LEN     33   // 16 bytes hex + null

// CA cert stored separately after ConfigData in EEPROM
#define MQTT_CA_CERT_MAX_LEN       2048
#define MQTT_CA_CERT_EEPROM_OFFSET sizeof(ConfigData)

struct __attribute__((packed)) WifiProfile {
    char ssid[WIFI_SSID_LEN];
    char password[WIFI_PASS_LEN];
};

struct __attribute__((packed)) VictronDeviceCfg {
    char name[VICTRON_NAME_LEN];        // friendly name
    char mac[VICTRON_MAC_LEN];          // BLE MAC address string
    char aesKey[VICTRON_KEY_LEN];       // 32-char hex AES key
    bool enabled;
};

struct __attribute__((packed)) ConfigData {
    uint16_t magic;
    uint8_t  version;

    // WiFi
    WifiProfile wifiProfiles[MAX_WIFI_PROFILES];
    uint8_t     wifiProfileCount;
    bool        apEnabled;
    char        apSsid[32];
    char        apPassword[65];

    // Victron devices
    VictronDeviceCfg victronDevices[MAX_VICTRON_DEVICES];
    uint8_t          victronDeviceCount;

    // MQTT
    char     mqttServer[64];
    char     mqttTopic[64];
    uint16_t mqttPort;
    uint16_t mqttInterval;   // seconds
    bool     mqttEnabled;

    // MQTT TLS / auth
    bool     mqttTlsEnabled;
    char     mqttUsername[64];
    char     mqttPassword[64];

    // Display / misc
    uint8_t  backlightPct;
    uint16_t displayTimeout; // seconds
};

// Init / persist
void configInit();
void configSave();
void configPrint();

// ── WiFi ──────────────────────────────────────────────────────────────────
uint8_t            configGetWifiProfileCount();
const WifiProfile& configGetWifiProfile(uint8_t idx);
void               configSetWifiProfile(uint8_t idx, const char* ssid, const char* pass);
void               configSetWifiProfileCount(uint8_t count);
bool               configGetApEnabled();
void               configSetApEnabled(bool v);
const char*        configGetApSsid();
void               configSetApSsid(const char* v);
const char*        configGetApPassword();
void               configSetApPassword(const char* v);

// ── Victron devices ───────────────────────────────────────────────────────
uint8_t                 configGetVictronCount();
void                    configSetVictronCount(uint8_t n);
const VictronDeviceCfg& configGetVictronDevice(uint8_t idx);
void                    configSetVictronDevice(uint8_t idx, const char* name,
                                               const char* mac, const char* aesKey,
                                               bool enabled);

// ── MQTT ──────────────────────────────────────────────────────────────────
const char* configGetMqttServer();
void        configSetMqttServer(const char* v);
const char* configGetMqttTopic();
void        configSetMqttTopic(const char* v);
uint16_t    configGetMqttPort();
void        configSetMqttPort(uint16_t v);
uint16_t    configGetMqttInterval();
void        configSetMqttInterval(uint16_t v);
bool        configGetMqttEnabled();
void        configSetMqttEnabled(bool v);

// MQTT TLS / auth
bool        configGetMqttTlsEnabled();
void        configSetMqttTlsEnabled(bool v);
const char* configGetMqttUsername();
void        configSetMqttUsername(const char* v);
const char* configGetMqttPassword();
void        configSetMqttPassword(const char* v);
void        configGetMqttCaCert(char* buf, size_t bufLen); // reads from EEPROM
void        configSetMqttCaCert(const char* pem);          // writes directly to EEPROM

// ── Display ───────────────────────────────────────────────────────────────
uint8_t  configGetBacklight();
void     configSetBacklight(uint8_t v);
uint16_t configGetDisplayTimeout();
void     configSetDisplayTimeout(uint16_t v);

const ConfigData& configGet();
