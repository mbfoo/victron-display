#include "watchdog.h"
#include "jbd_bms.h"
#include "esp_task_wdt.h"
#include "history_sd.h"

// ─── Internal state ───────────────────────────────────────────────────────────
static uint32_t s_lastCheckMs        = 0;
static uint32_t s_bmsDisconnectedMs  = 0;   // millis() when BMS last went non-CONNECTED
static bool     s_bmsWasConnected    = false;
static bool     s_bmsEverConnected   = false;

// Grace period before the BMS timeout starts counting.
// Gives BLE scanning time to find the device on first boot.
static constexpr uint32_t BMS_GRACE_PERIOD_MS = 60000;   // 1 minute
static uint32_t s_bootMs = 0;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static void triggerReboot(const char* reason) {
    historySDSave();
    Serial.printf("\n[WDT] *** REBOOTING: %s ***\n", reason);
    Serial.flush();
    delay(200);
    esp_restart();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public: init / task
// ═══════════════════════════════════════════════════════════════════════════════

void watchdogInit() {
    s_bootMs             = millis();
    s_bmsDisconnectedMs  = millis();   // start the clock from boot
    s_bmsWasConnected    = false;
    s_bmsEverConnected   = false;

    // Initialise hardware task watchdog
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms     = WDT_HW_TIMEOUT_S * 1000,
        .idle_core_mask = 0,           // don't watch idle tasks
        .trigger_panic  = true         // trigger panic (and reboot) on timeout
    };
    esp_err_t err = esp_task_wdt_reconfigure(&wdt_config);
    if (err != ESP_OK) {
        // WDT not yet initialised on this build — init it fresh
        esp_task_wdt_init(&wdt_config);
    }

    // Subscribe the current (main loop) task to the watchdog
    esp_task_wdt_add(NULL);

    Serial.printf("[WDT] Init  hw_timeout=%ds  bms_timeout=%lus  grace=%lus\n",
                  WDT_HW_TIMEOUT_S,
                  WDT_BMS_TIMEOUT_MS  / 1000UL,
                  BMS_GRACE_PERIOD_MS / 1000UL);
}

void watchdogTask() {
    uint32_t now = millis();

    // ── Feed the hardware WDT every loop tick ─────────────────────────────────
    // As long as loop() is running, this resets the HW timer.
    // If loop() hangs, the timer fires and the chip hard-resets.
    esp_task_wdt_reset();

    // ── Throttle the BMS connection check ────────────────────────────────────
    if (now - s_lastCheckMs < WDT_CHECK_INTERVAL_MS) return;
    s_lastCheckMs = now;

    bool bmsConnected = (bmsGetState() == BmsState::CONNECTED) || bmsIsSimulation();

    // ── Track transitions ─────────────────────────────────────────────────────
    if (bmsConnected) {
        if (!s_bmsWasConnected) {
            Serial.println("[WDT] BMS connected – BMS timeout reset");
        }
        s_bmsWasConnected   = true;
        s_bmsEverConnected  = true;
        s_bmsDisconnectedMs = now;   // keep updating while connected
    } else {
        if (s_bmsWasConnected) {
            Serial.println("[WDT] BMS disconnected – starting timeout timer");
            s_bmsDisconnectedMs = now;
        }
        s_bmsWasConnected = false;
    }

    // ── BMS timeout logic ─────────────────────────────────────────────────────
    uint32_t disconnectedFor = now - s_bmsDisconnectedMs;

    // Skip during grace period (first boot scan window)
    if (!s_bmsEverConnected && now - s_bootMs < BMS_GRACE_PERIOD_MS) {
        uint32_t graceLeft = (BMS_GRACE_PERIOD_MS - (now - s_bootMs)) / 1000;
        if (disconnectedFor % 15000 < WDT_CHECK_INTERVAL_MS) {
            Serial.printf("[WDT] BMS not yet connected – grace period: %lus remaining\n",
                          graceLeft);
        }
        return;
    }

    // Warn in the last 10 seconds before reboot
    if (disconnectedFor >= WDT_BMS_TIMEOUT_MS - 10000 &&
        disconnectedFor <  WDT_BMS_TIMEOUT_MS) {
        uint32_t secsLeft = (WDT_BMS_TIMEOUT_MS - disconnectedFor) / 1000;
        Serial.printf("[WDT] BMS still not connected – reboot in %lus\n", secsLeft);
    }

    if (disconnectedFor >= WDT_BMS_TIMEOUT_MS) {
        triggerReboot("BMS not connected for 5 minutes");
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public: API
// ═══════════════════════════════════════════════════════════════════════════════

uint32_t watchdogGetBmsDisconnectedMs() {
    if (bmsIsSimulation()) return 0;
    if (bmsGetState() == BmsState::CONNECTED) return 0;
    return millis() - s_bmsDisconnectedMs;
}

bool watchdogIsBmsResetImminent() {
    return watchdogGetBmsDisconnectedMs() >= WDT_BMS_TIMEOUT_MS - 10000;
}

void watchdogPrint() {
    uint32_t disconnMs = watchdogGetBmsDisconnectedMs();
    Serial.println("\n======= WATCHDOG =======");
    Serial.printf("  HW timeout      : %d s\n",    WDT_HW_TIMEOUT_S);
    Serial.printf("  BMS timeout     : %lu s\n",   WDT_BMS_TIMEOUT_MS  / 1000UL);
    Serial.printf("  Grace period    : %lu s\n",   BMS_GRACE_PERIOD_MS / 1000UL);
    Serial.printf("  BMS connected   : %s\n",
                  bmsGetState() == BmsState::CONNECTED ? "YES" : "NO");
    Serial.printf("  Disconnected for: %lu s\n",   disconnMs / 1000UL);
    Serial.printf("  Reset imminent  : %s\n",
                  watchdogIsBmsResetImminent() ? "YES" : "no");
    Serial.println("========================\n");
}
