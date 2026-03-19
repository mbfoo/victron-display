#pragma once
#include <Arduino.h>

// ─── Tuning ───────────────────────────────────────────────────────────────────

/** EMA time constant in seconds. Larger = smoother but slower to react. */
#ifndef BAT_EST_TAU_S
  #define BAT_EST_TAU_S          60.0f
#endif

/**
 * Minimum absolute current (A) to consider the battery actually
 * charging/discharging. Below this the estimates are marked invalid
 * to avoid division-by-near-zero (e.g. idle float current on a full pack).
 */
#ifndef BAT_EST_MIN_CURRENT_A
  #define BAT_EST_MIN_CURRENT_A   0.1f
#endif

/** How often (ms) the estimator recalculates. */
#ifndef BAT_EST_UPDATE_INTERVAL_MS
  #define BAT_EST_UPDATE_INTERVAL_MS  1000
#endif

// ─── Result struct ────────────────────────────────────────────────────────────

struct BatEstimate {
    // Low-pass filtered current [A]  (negative = discharging, positive = charging)
    float    filteredCurrent_A;

    // Remaining discharge time (only valid when discharging)
    float    remainingDischargeTime_h;    // hours
    uint32_t remainingDischargeTime_s;    // seconds (convenience)
    bool     dischargeTimeValid;          // false when idle or charging

    // Remaining charge time (only valid when charging)
    float    remainingChargeTime_h;       // hours
    uint32_t remainingChargeTime_s;       // seconds (convenience)
    bool     chargeTimeValid;             // false when idle or discharging

    // Instantaneous power [W]
    float    power_W;

    // Energy remaining [Wh]  (from BMS remainCapacity_Ah × pack voltage)
    float    remainingEnergy_Wh;

    // Energy still to charge [Wh]
    float    energyToFull_Wh;

    // Whether the estimator has enough data to produce valid results
    bool     valid;
};

// ─── Init / task ──────────────────────────────────────────────────────────────

/**
 * Call once from setup().
 * @param tauSeconds  EMA time constant. Pass 0 to use BAT_EST_TAU_S default.
 */
void batEstInit(float tauSeconds = 0.0f);

/** Call every loop(). Non-blocking – recalculates on BAT_EST_UPDATE_INTERVAL_MS cadence. */
void batEstTask();

// ─── API ──────────────────────────────────────────────────────────────────────

/** Full result snapshot. */
const BatEstimate& batEstGet();

/** Low-pass filtered current [A]. Negative = discharging. */
float    batEstGetFilteredCurrent();

/** Estimated remaining discharge time in seconds. 0 if not discharging. */
uint32_t batEstGetRemainingDischargeSeconds();

/** Estimated remaining discharge time in hours. */
float    batEstGetRemainingDischargeHours();

/** Estimated remaining charge time in seconds. 0 if not charging. */
uint32_t batEstGetRemainingChargeSeconds();

/** Estimated remaining charge time in hours. */
float    batEstGetRemainingChargeHours();

/** Instantaneous power [W]. Negative = discharging. */
float    batEstGetPower();

/** Remaining energy [Wh]. */
float    batEstGetRemainingEnergy();

/** True when at least one valid estimate has been produced. */
bool     batEstIsValid();

/**
 * Helper: format seconds → "Xh Ym Zs" string.
 * Writes into caller-supplied buffer of at least 24 bytes.
 */
void     batEstFormatTime(uint32_t seconds, char* buf, size_t bufLen);

/** Print current estimates to Serial. */
void     batEstPrint();

void batEstSetTau(float tauSeconds);