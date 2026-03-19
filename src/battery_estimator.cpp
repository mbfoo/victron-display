#include "battery_estimator.h"
#include "jbd_bms.h"

// ─── Internal state ───────────────────────────────────────────────────────────
static BatEstimate s_est;
static float       s_tau           = BAT_EST_TAU_S;
static float       s_emaAlpha      = 0.0f;    // recalculated on first tick
static float       s_filteredCurrent = 0.0f;
static bool        s_firstSample   = true;
static uint32_t    s_lastUpdateMs  = 0;
static uint32_t    s_lastBmsMs     = 0;       // millis of last valid BMS read

// ─── EMA low-pass filter ──────────────────────────────────────────────────────
// alpha = dt / (tau + dt)
// When alpha→1: no filtering (instant).  alpha→0: heavy smoothing.
static float calcAlpha(float dt_s) {
    if (s_tau <= 0.0f) return 1.0f;
    return dt_s / (s_tau + dt_s);
}

// ─── Format helper ────────────────────────────────────────────────────────────
void batEstFormatTime(uint32_t secs, char* buf, size_t bufLen) {
    uint32_t h = secs / 3600;
    uint32_t m = (secs % 3600) / 60;
    uint32_t s = secs % 60;
    if (h > 0)
        snprintf(buf, bufLen, "%lu hours", h);
    else if (m > 0)
        snprintf(buf, bufLen, "%lu minutes", m);
    else
        snprintf(buf, bufLen, "%lu seconds", s);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public: init / task
// ═══════════════════════════════════════════════════════════════════════════════

void batEstInit(float tauSeconds) {
    s_tau         = (tauSeconds > 0.0f) ? tauSeconds : BAT_EST_TAU_S;
    s_firstSample = true;
    memset(&s_est, 0, sizeof(s_est));

    Serial.printf("[BAT EST] Init  tau=%.1fs  minCurrent=%.2fA  interval=%dms\n",
                  s_tau, BAT_EST_MIN_CURRENT_A, BAT_EST_UPDATE_INTERVAL_MS);
}

void batEstTask() {
    uint32_t now = millis();
    if (now - s_lastUpdateMs < BAT_EST_UPDATE_INTERVAL_MS) return;

    float dt_s = (s_lastUpdateMs == 0)
                 ? (BAT_EST_UPDATE_INTERVAL_MS / 1000.0f)
                 : ((now - s_lastUpdateMs) / 1000.0f);
    s_lastUpdateMs = now;

    // ── Require valid BMS data ────────────────────────────────────────────────
    if (!bmsIsDataValid()) {
        // Serial.println("[BAT EST] Waiting for valid BMS data...");
        s_est.valid = false;
        return;
    }

    const BmsData& bms = bmsGetData();

    // ── EMA low-pass filter on current ────────────────────────────────────────
    float rawCurrent = bms.basic.current_A;
    s_emaAlpha       = calcAlpha(dt_s);

    if (s_firstSample) {
        // Seed filter with first real reading to avoid a long ramp-up
        s_filteredCurrent = rawCurrent;
        s_firstSample     = false;
        Serial.printf("[BAT EST] Filter seeded  I=%.3fA\n", rawCurrent);
    } else {
        s_filteredCurrent = s_emaAlpha * rawCurrent
                          + (1.0f - s_emaAlpha) * s_filteredCurrent;
    }

    // ── Capacity figures from BMS ─────────────────────────────────────────────
    float remainCap_Ah  = bms.basic.remainCapacity_Ah;    // Ah left
    float nominalCap_Ah = bms.basic.nominalCapacity_Ah;   // total rated Ah
    uint8_t soc         = bms.basic.stateOfCharge_pct;
    float voltage       = bms.basic.totalVoltage_V;

    // Capacity still to charge (Ah needed to reach 100 %)
    float toFull_Ah = nominalCap_Ah * (100.0f - soc) / 100.0f;

    // ── Power & energy ────────────────────────────────────────────────────────
    s_est.filteredCurrent_A   = s_filteredCurrent;
    s_est.power_W             = s_filteredCurrent * voltage;
    s_est.remainingEnergy_Wh  = remainCap_Ah  * voltage;
    s_est.energyToFull_Wh     = toFull_Ah     * voltage;

    // ── Discharge estimate ────────────────────────────────────────────────────
    // current_A < 0 means discharging (JBD convention)
    float dischargeCurrent = -s_filteredCurrent;   // positive when discharging

    if (dischargeCurrent >= BAT_EST_MIN_CURRENT_A && remainCap_Ah > 0.0f) {
        s_est.remainingDischargeTime_h = remainCap_Ah / dischargeCurrent;
        s_est.remainingDischargeTime_s =
            (uint32_t)(s_est.remainingDischargeTime_h * 3600.0f);
        s_est.dischargeTimeValid = true;
    } else {
        s_est.remainingDischargeTime_h = 0.0f;
        s_est.remainingDischargeTime_s = 0;
        s_est.dischargeTimeValid       = false;
    }

    // ── Charge estimate ───────────────────────────────────────────────────────
    // current_A > 0 means charging
    float chargeCurrent = s_filteredCurrent;       // positive when charging

    if (chargeCurrent >= BAT_EST_MIN_CURRENT_A && toFull_Ah > 0.0f) {
        s_est.remainingChargeTime_h = toFull_Ah / chargeCurrent;
        s_est.remainingChargeTime_s =
            (uint32_t)(s_est.remainingChargeTime_h * 3600.0f);
        s_est.chargeTimeValid = true;
    } else {
        s_est.remainingChargeTime_h = 0.0f;
        s_est.remainingChargeTime_s = 0;
        s_est.chargeTimeValid       = false;
    }

    s_est.valid = true;

    // Serial.printf("[BAT EST] I_raw=%.3fA  I_filt=%.3fA  alpha=%.4f  "
    //               "SOC=%d%%  remain=%.3fAh  toFull=%.3fAh\n",
    //               rawCurrent, s_filteredCurrent, s_emaAlpha,
    //               soc, remainCap_Ah, toFull_Ah);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public: API
// ═══════════════════════════════════════════════════════════════════════════════

const BatEstimate& batEstGet()                    { return s_est; }
float    batEstGetFilteredCurrent()               { return s_est.filteredCurrent_A; }
float    batEstGetRemainingDischargeHours()        { return s_est.remainingDischargeTime_h; }
uint32_t batEstGetRemainingDischargeSeconds()      { return s_est.remainingDischargeTime_s; }
float    batEstGetRemainingChargeHours()           { return s_est.remainingChargeTime_h; }
uint32_t batEstGetRemainingChargeSeconds()         { return s_est.remainingChargeTime_s; }
float    batEstGetPower()                          { return s_est.power_W; }
float    batEstGetRemainingEnergy()                { return s_est.remainingEnergy_Wh; }
bool     batEstIsValid()                           { return s_est.valid; }

void batEstPrint() {
    char buf[24];
    Serial.println("\n======= BATTERY ESTIMATE =======");
    if (!s_est.valid) { Serial.println("  (no valid data yet)"); Serial.println("================================\n"); return; }

    Serial.printf("  Filtered current : %+.3f A  (%s)\n",
                  s_est.filteredCurrent_A,
                  s_est.filteredCurrent_A < -BAT_EST_MIN_CURRENT_A ? "discharging" :
                  s_est.filteredCurrent_A >  BAT_EST_MIN_CURRENT_A ? "charging"    : "idle");
    Serial.printf("  Power            : %+.1f W\n", s_est.power_W);
    Serial.printf("  Energy remaining : %.1f Wh\n", s_est.remainingEnergy_Wh);
    Serial.printf("  Energy to full   : %.1f Wh\n", s_est.energyToFull_Wh);

    if (s_est.dischargeTimeValid) {
        batEstFormatTime(s_est.remainingDischargeTime_s, buf, sizeof(buf));
        Serial.printf("  Discharge time   : %s  (%.2f h)\n",
                      buf, s_est.remainingDischargeTime_h);
    } else {
        Serial.println("  Discharge time   : N/A");
    }

    if (s_est.chargeTimeValid) {
        batEstFormatTime(s_est.remainingChargeTime_s, buf, sizeof(buf));
        Serial.printf("  Charge time      : %s  (%.2f h)\n",
                      buf, s_est.remainingChargeTime_h);
    } else {
        Serial.println("  Charge time      : N/A");
    }
    Serial.println("================================\n");
}

void batEstSetTau(float tauSeconds) {
    if (tauSeconds < 1.0f) tauSeconds = 1.0f;
    if (tauSeconds > 600.0f) tauSeconds = 600.0f;
    s_tau = tauSeconds;
    // Do NOT reset s_firstSample — filter keeps running smoothly
    Serial.printf("[BAT EST] tau updated to %.1f s  (alpha@1s=%.4f)\n",
        s_tau, calcAlpha(1.0f));
}
