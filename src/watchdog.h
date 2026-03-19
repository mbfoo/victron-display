#pragma once
#include <Arduino.h>

// ─── Timing constants ─────────────────────────────────────────────────────────

/** Hardware WDT timeout — device resets if watchdog is not fed within this time. */
#ifndef WDT_HW_TIMEOUT_S
  #define WDT_HW_TIMEOUT_S          30
#endif

/** How long (ms) to tolerate no BMS connection before forcing a reboot. */
#ifndef WDT_BMS_TIMEOUT_MS
  #define WDT_BMS_TIMEOUT_MS       (5UL * 60UL * 1000UL)   // 5 minutes
#endif

/** How often (ms) the watchdog task runs its checks. */
#ifndef WDT_CHECK_INTERVAL_MS
  #define WDT_CHECK_INTERVAL_MS     1000
#endif

// ─── Init / task ──────────────────────────────────────────────────────────────

/**
 * Call once from setup().
 * Initialises and starts the hardware watchdog timer.
 */
void watchdogInit();

/**
 * Call every loop(). Non-blocking.
 * - Feeds the hardware WDT (prevents hang reset as long as loop() runs)
 * - Monitors BMS connection and triggers a reboot after WDT_BMS_TIMEOUT_MS
 */
void watchdogTask();

// ─── API ──────────────────────────────────────────────────────────────────────

/** Milliseconds since BMS was last connected. */
uint32_t watchdogGetBmsDisconnectedMs();

/** True if the BMS timeout reset is imminent (last 10 seconds). */
bool watchdogIsBmsResetImminent();

/** Print current watchdog status to Serial. */
void watchdogPrint();
