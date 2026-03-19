#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>


// ─── Public data structs ──────────────────────────────────────────────────────

struct BmsBasicInfo {
    float    totalVoltage_V;
    float    current_A;
    float    remainCapacity_Ah;
    float    nominalCapacity_Ah;
    uint16_t cycleCount;
    uint8_t  stateOfCharge_pct;
    uint8_t  numCells;
    uint8_t  numNTC;
    float    temperature_C[8];
    uint16_t protectionStatus;
    uint8_t  fetStatus;
    bool     valid;
};

struct BmsCellData {
    uint8_t  cellCount;
    uint16_t cellVoltage_mV[32];
    bool     valid;
};

struct BmsData {
    BmsBasicInfo basic;
    BmsCellData  cells;
    uint32_t     lastUpdateMs;   // millis() of last successful full update
};

// ─── Connection state (readable by other modules) ────────────────────────────

enum class BmsState {
    SCANNING,
    CONNECTING,
    CONNECTED,
    DISCONNECTED
};

// ─── Init / task ──────────────────────────────────────────────────────────────

/**
 * Call once from setup(). Initialises BLE and starts the first scan.
 * @param deviceName  Partial name to match against BLE advertisements.
 * @param pollMs      How often (ms) to request fresh data once connected.
 */
void bmsInit(const char* deviceName = "JBD-SP", uint32_t pollMs = 5000);

/**
 * Call every iteration of loop(). Non-blocking – returns immediately.
 * Drives scanning, connection, polling, and response parsing.
 */
void bmsTask();

// ─── API: data access ─────────────────────────────────────────────────────────

/** Returns a const reference to the latest BMS data snapshot. */
const BmsData& bmsGetData();

/** True when a complete basic-info + cell-voltage update has been received. */
bool bmsIsDataValid();

/** Current connection/scan state. */
BmsState bmsGetState();

/** Convenience getters – return 0 / NaN when no valid data available. */
float    bmsGetVoltage();          // pack voltage [V]
float    bmsGetCurrent();          // current [A], negative = discharging
uint8_t  bmsGetSOC();              // state of charge [%]
float    bmsGetTemperature(uint8_t idx = 0); // temperature sensor idx [°C]
uint16_t bmsGetCellVoltage(uint8_t idx);     // single cell [mV]
bool     bmsIsCharging();
bool     bmsIsDischarging();
bool     bmsChargeFetOn();
bool     bmsdischargeFetOn();

// ─── API: FET control ─────────────────────────────────────────────────────────

/**
 * Control charge and discharge MOSFETs independently.
 * Changes are sent immediately if connected; queued and replayed on next
 * connection if not.
 */
void bmsSetChargeFet(bool enable);
void bmsSetDischargeFet(bool enable);

/**
 * Set both FETs in one command.
 * @param chargeEnable    true = charge FET ON
 * @param dischargeEnable true = discharge FET ON
 */
void bmsSetFets(bool chargeEnable, bool dischargeEnable);

/** Re-applies the last requested FET state (useful after reconnect). */
void bmsApplyFetState();

/** Enable / disable simulation mode. When on, BLE is bypassed entirely and
 *  synthetic data is injected every poll interval instead.            */
void bmsSetSimulation(bool enable);
bool bmsIsSimulation();