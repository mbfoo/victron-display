#pragma once
#include <Arduino.h>

// ─── Hysteresis ───────────────────────────────────────────────────────────────
/**
 * Once the charge FET has been cut, charging only re-enables when SOC drops
 * back below (maxChargeSoc - SOC_LIMITER_HYSTERESIS_PCT).
 * Prevents rapid toggling near the threshold.
 */
#ifndef SOC_LIMITER_HYSTERESIS_PCT
  #define SOC_LIMITER_HYSTERESIS_PCT   3
#endif

/** How often (ms) the limiter checks SOC. */
#ifndef SOC_LIMITER_CHECK_INTERVAL_MS
  #define SOC_LIMITER_CHECK_INTERVAL_MS  2000
#endif

// ─── State ────────────────────────────────────────────────────────────────────
enum class SocLimiterState {
    LIMITER_DISABLED,   // feature turned off in config
    MONITORING,         // SOC below limit, charge FET left alone
    LIMITING,           // SOC at/above limit, charge FET held OFF
    WAITING_FOR_BMS,    // no valid BMS data yet
};

// ─── Init / task ──────────────────────────────────────────────────────────────

/** Call once from setup(). */
void socLimiterInit();

/** Call every loop(). Non-blocking. */
void socLimiterTask();

// ─── API ──────────────────────────────────────────────────────────────────────

SocLimiterState socLimiterGetState();
bool            socLimiterIsLimiting();     // true when FET is being held OFF
uint8_t         socLimiterGetThreshold();   // current active threshold %
uint8_t         socLimiterGetResumeAt();    // SOC at which charging resumes

/**
 * Notify the limiter that config has changed at runtime
 * (e.g. user updated maxChargeSoc or toggled the feature via serial/MQTT).
 * Re-evaluates state immediately.
 */
void socLimiterApplyConfig();

/** Print current state to Serial. */
void socLimiterPrint();

// Called by any code that manually sets a FET (web UI, display)
// Suspends the limiter until socLimiterApplyConfig() is called again.
void socLimiterManualFetOverride();
bool socLimiterIsEnabled();
void socLimiterSetEnabled(bool enable);

