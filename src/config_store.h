#pragma once
#include <Arduino.h>

#define CONFIG_MAGIC      0xBEEF
#define CONFIG_VERSION    12  // bumped for WiFi profiles
#define MAX_WIFI_PROFILES 10
#define WIFI_SSID_LEN     32
#define WIFI_PASS_LEN     64

struct __attribute__((packed)) WifiProfile {
    char ssid[WIFI_SSID_LEN];
    char password[WIFI_PASS_LEN];
};

struct __attribute__((packed)) ConfigData {
    uint16_t  magic;
    uint8_t   version;
    
    // WiFi profiles (10 × (32+64)=960 bytes)
    WifiProfile wifiProfiles[MAX_WIFI_PROFILES];
    uint8_t     wifiProfileCount;  // 0-10, how many valid profiles
    
    // Other settings (unchanged)
    char bmsDeviceName[32];
    char mqttServer[64];
    char mqttTopic[64];
    uint8_t maxChargeSoc;
    bool    socLimitEnabled;
    
    // AP settings
    bool    apEnabled;
    char    apSsid[32];
    char    apPassword[64];
    
    // Other settings...
    uint8_t  backlightPct;
    float    estTauSeconds;
    uint16_t bmsPollInterval;
    bool     mqttEnabled;
    uint16_t mqttPort;
    uint16_t mqttInterval;
    uint16_t displayTimeout;
};

// Init/load/save
void configInit();
void configSave();
void configPrint();

// Getters
const ConfigData& configGet();
uint8_t            configGetWifiProfileCount();
const WifiProfile& configGetWifiProfile(uint8_t idx);
const char*        configGetBmsDeviceName();
const char*        configGetMqttServer();
const char*        configGetMqttTopic();
uint8_t            configGetMaxChargeSoc();
bool               configGetSocLimitEnabled();
bool               configGetApEnabled();
const char*        configGetApSsid();
const char*        configGetApPassword();
uint8_t            configGetBacklight();
float              configGetEstTau();
uint16_t           configGetBmsPollInterval();
bool               configGetMqttEnabled();
uint16_t           configGetMqttPort();
uint16_t           configGetMqttInterval();
uint16_t           configGetDisplayTimeout();

// Setters
void configSetWifiProfile(uint8_t idx, const char* ssid, const char* pass);
void configSetWifiProfileCount(uint8_t count);
void configSetBmsDeviceName(const char* v);
void configSetMqttServer(const char* v);
void configSetMqttTopic(const char* v);
void configSetMaxChargeSoc(uint8_t v);
void configSetSocLimitEnabled(bool v);
void configSetApEnabled(bool v);
void configSetApSsid(const char* v);
void configSetApPassword(const char* v);
void configSetBacklight(uint8_t v);
void configSetEstTau(float t);
void configSetBmsPollInterval(uint16_t v);
void configSetMqttEnabled(bool v);
void configSetMqttPort(uint16_t v);
void configSetMqttInterval(uint16_t v);
void configSetDisplayTimeout(uint16_t v);
