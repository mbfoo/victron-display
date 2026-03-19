#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MAX_VICTRON_DEVICES 5

enum class VictronChargerState : uint8_t {
    Off              = 0,
    LowPower         = 1,
    Fault            = 2,
    Bulk             = 3,
    Absorption       = 4,
    Float            = 5,
    Storage          = 6,
    Equalize         = 7,
    Inverting        = 9,
    PowerSupply      = 11,
    StartingUp       = 245,
    RepeatedAbsorption = 247,
    AutoEqualize     = 248,
    BatterySafe      = 249,
    ExternalControl  = 252,
    Unknown          = 0xFF
};

enum class VictronErrorCode : uint8_t {
    None                      = 0,
    BatteryHighVoltage         = 2,
    BatteryLowVoltage          = 3,
    RemoteTemperatureShutdown  = 4,
    RemoteBatteryVoltage       = 5,
    HighSolarVoltageShutdown   = 17,
    WrongSolarVoltage          = 18,
    SolarPanelOverload         = 20,
    SolarPanelReversedPolarity = 33,
    BulkTimeExpired            = 38,
    CurrentSensorIssue         = 40,
    TerminalOverheated         = 116,
    Unknown                    = 0xFF
};

struct VictronMpptData {
    bool     valid;
    uint32_t lastUpdateMs;

    VictronChargerState chargerState;
    VictronErrorCode    errorCode;
    float batteryVoltage_V;  // 0.01 V resolution
    float batteryCurrent_A;  // 0.1 A resolution, signed
    float pvPower_W;         // 1 W resolution
    float yieldToday_kWh;    // 0.01 kWh resolution
    uint16_t nonce;

    char  name[32];
    char  mac[18];
    int8_t rssi;
};

void                   victronBleInit();
void                   victronBleTask();
const VictronMpptData* victronBleGetDevices();
uint8_t                victronBleGetDeviceCount();
float                  victronBleGetTotalPvPower();
void                   victronBleApplyConfig();
void                   victronBlePrint();
