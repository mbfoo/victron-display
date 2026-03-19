#include "soc_limiter.h"
#include "jbd_bms.h"
#include "config_store.h"

#define SOC_LIMITER_HYSTERESIS_PCT     3

static SocLimiterState s_state    = SocLimiterState::WAITING_FOR_BMS;
static uint32_t        s_lastCheckMs = 0;
static bool            s_enabled  = false;  // RAM copy — decoupled from EEPROM

static uint8_t threshold() { return configGetMaxChargeSoc(); }
static uint8_t resumeAt()  {
    int r = (int)threshold() - SOC_LIMITER_HYSTERESIS_PCT;
    return (uint8_t)(r < 1 ? 1 : r);
}

// ── Public: called when user explicitly enables/disables via display or GUI ──
void socLimiterSetEnabled(bool enable) {
    s_enabled = enable;
    configSetSocLimitEnabled(enable);
    configSave();
    Serial.printf("[SOC LIM] User set enabled=%s (saved to EEPROM)\n",
                  enable ? "YES" : "NO");
    socLimiterApplyConfig();
}

// ── Public: called when user manually touches charge FET switch ──────────────
// Disables limiter in RAM only — EEPROM unchanged, re-enable is explicit.
void socLimiterManualFetOverride() {
    if (!s_enabled) return;  // already disabled, nothing to do
    s_enabled = false;
    s_state   = SocLimiterState::LIMITER_DISABLED;
    Serial.println("[SOC LIM] Manual FET override — limiter suspended (RAM only)");
    Serial.println("[SOC LIM] Re-enable limiter switch to restore automatic control");
}

static void evaluate() {
    if (!bmsIsDataValid()) {
        s_state = SocLimiterState::WAITING_FOR_BMS;
        return;
    }
    if (!s_enabled) {
        s_state = SocLimiterState::LIMITER_DISABLED;
        return;
    }

    uint8_t soc = bmsGetSOC();

    switch (s_state) {
    case SocLimiterState::WAITING_FOR_BMS:
    case SocLimiterState::LIMITER_DISABLED:
        s_state = SocLimiterState::MONITORING;
        // fall through

    case SocLimiterState::MONITORING:
        if (soc >= threshold()) {
            Serial.printf("[SOC LIM] SOC %d%% >= limit %d%% — cutting charge FET\n",
                          soc, threshold());
            bmsSetChargeFet(false);
            s_state = SocLimiterState::LIMITING;
        }
        break;

    case SocLimiterState::LIMITING:
        if (soc <= resumeAt()) {
            Serial.printf("[SOC LIM] SOC %d%% <= resume %d%% — restoring charge FET\n",
                          soc, resumeAt());
            bmsSetChargeFet(true);
            s_state = SocLimiterState::MONITORING;
        }
        break;
    }
}

void socLimiterInit() {
    s_enabled    = configGetSocLimitEnabled();
    s_lastCheckMs = 0;
    s_state      = SocLimiterState::WAITING_FOR_BMS;
    Serial.printf("[SOC LIM] Init: %s  threshold=%d%%  resumeAt=%d%%\n",
                  s_enabled ? "ENABLED" : "DISABLED", threshold(), resumeAt());
}

void socLimiterTask() {
    uint32_t now = millis();
    if (now - s_lastCheckMs < SOC_LIMITER_CHECK_INTERVAL_MS) return;
    s_lastCheckMs = now;
    evaluate();
}

void socLimiterApplyConfig() {
    s_enabled = configGetSocLimitEnabled();
    Serial.printf("[SOC LIM] Config applied: %s  threshold=%d%%\n",
                  s_enabled ? "ENABLED" : "DISABLED", threshold());
    // If enabling and SOC already below threshold, make sure charge FET is on
    if (s_enabled && bmsIsDataValid() && bmsGetSOC() < threshold()) {
        bmsSetChargeFet(true);
    }
    s_state = s_enabled ? SocLimiterState::MONITORING : SocLimiterState::LIMITER_DISABLED;
}

// Getters
SocLimiterState socLimiterGetState()    { return s_state; }
bool            socLimiterIsLimiting()  { return s_state == SocLimiterState::LIMITING; }
bool            socLimiterIsEnabled()   { return s_enabled; }
uint8_t         socLimiterGetThreshold(){ return threshold(); }
uint8_t         socLimiterGetResumeAt() { return resumeAt(); }

void socLimiterPrint() {
    static const char* st[] = {"DISABLED","MONITORING","LIMITING","WAITING_FOR_BMS"};
    Serial.println("=== SOC LIMITER ===");
    Serial.printf("  State    : %s\n", st[(int)s_state]);
    Serial.printf("  Enabled  : %s\n", s_enabled ? "YES (RAM)" : "NO");
    Serial.printf("  EEPROM   : %s\n", configGetSocLimitEnabled() ? "YES" : "NO");
    Serial.printf("  Threshold: %d%%\n", threshold());
    Serial.printf("  Resume at: %d%%\n", resumeAt());
    if (bmsIsDataValid()) Serial.printf("  SOC now  : %d%%\n", bmsGetSOC());
    Serial.println("==================");
}
