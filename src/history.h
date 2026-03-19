#pragma once
#include <Arduino.h>

// ─── Tier configuration ───────────────────────────────────────────────────────
#define HISTORY_TIER_COUNT      3

#define HISTORY_T0_INTERVAL_S   10      // sample every 10 s
#define HISTORY_T0_CAPACITY     360     // 10s × 360 = 1 hour

#define HISTORY_T1_INTERVAL_S   60      // 1 min
#define HISTORY_T1_CAPACITY     1440    // 1min × 1440 = 24 hours

#define HISTORY_T2_INTERVAL_S   600     // 10 min
#define HISTORY_T2_CAPACITY     1008    // 10min × 1008 = 7 days

// ─── Data point ───────────────────────────────────────────────────────────────
// 17 bytes packed — 2808 total points × 17 = ~47 KB static RAM
struct __attribute__((packed)) HistoryPoint {
    uint32_t ts;            // seconds since boot
    uint8_t  soc;           // 0–100 %
    int16_t  current_cA;    // current × 100  (÷100.0f = Amps)
    uint16_t cell_mV[4];    // individual cell voltages in mV
    uint16_t voltage_mV;
};

// ─── Public API ───────────────────────────────────────────────────────────────

// Call once from setup(), after configInit() and bmsInit()
void historyInit();

// Call every loop() iteration — non-blocking
void historyTask();

// Copy up to maxCount points from tier into outBuf, oldest first.
// Returns actual count written.
uint16_t historyGetPoints(uint8_t tier, HistoryPoint* outBuf, uint16_t maxCount);

// Copy up to maxCount points whose ts falls within [fromSec, toSec].
// Selects the best tier automatically. Returns actual count written.
// Pass fromSec=0 / toSec=UINT32_MAX to get everything from a tier.
uint16_t historyGetRange(uint32_t fromSec, uint32_t toSec,
                         HistoryPoint* outBuf, uint16_t maxCount,
                         uint8_t* outTierUsed);

// Metadata
uint16_t historyGetCount(uint8_t tier);
uint32_t historyGetInterval(uint8_t tier);  // seconds per point for this tier
uint32_t historyGetNewestTs();              // ts of most recent point across all tiers
uint32_t historyGetOldestTs();              // ts of oldest available point

// Debug
void historyPrint();


// Opaque ring accessor used by the web server for efficient streaming.
// Do not use outside of web_server.cpp.
struct RingBuf_t {
    HistoryPoint* buf;
    uint16_t      capacity;
    uint16_t      head;
    uint16_t      count;
};
const RingBuf_t* historyGetRingBuf(uint8_t tier);

// Restore a full tier from saved data (oldest-first array of count points).
// Clears the existing ring buffer before loading.
void historyRestorePoints(uint8_t tier, const HistoryPoint* pts, uint16_t count);

// Time offset — added to millis()/1000 for all new samples.
// Set to the last saved timestamp after an SD restore so new points
// continue in sequence instead of restarting from t=0.
void     historySetTimeOffset(uint32_t offset);
uint32_t historyGetTimeOffset();

// Use this everywhere instead of millis()/1000 directly.
uint32_t historyNow();