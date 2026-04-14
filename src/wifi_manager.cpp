#include "wifi_manager.h"
#include "config_store.h"
#include <WiFi.h>
#include <vector>

static WifiState s_state          = WifiState::IDLE;
static uint32_t  s_stateEnteredMs = 0;
static uint32_t  s_lastMonitorMs  = 0;
static uint32_t  s_connectedSince = 0;
static uint8_t   s_retryCount     = 0;
static bool      s_scanPending    = false;  // async scan in progress

static void enterState(WifiState next) {
    s_state          = next;
    s_stateEnteredMs = millis();
}
static uint32_t elapsed() { return millis() - s_stateEnteredMs; }

static void beginConnect() {
    uint8_t count = configGetWifiProfileCount();
    for (uint8_t p = 0; p < count; p++) {
        const WifiProfile& profile = configGetWifiProfile(p);
        if (strlen(profile.ssid) == 0) continue;
        Serial.printf("[WIFI] Connecting to '%s' (profile %d)\n",
                      profile.ssid, p);
        WiFi.disconnect(false);
        WiFi.begin(profile.ssid, profile.password);
        enterState(WifiState::CONNECTING);
        return;
    }
    Serial.println("[WIFI] No valid profiles, entering AP mode");
    if (configGetApEnabled()) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(configGetApSsid(), configGetApPassword());
        enterState(WifiState::AP);
    }
}

// ── Async scan helpers ────────────────────────────────────────────────────────

static void startAsyncScan() {
    Serial.println("[WIFI] Starting async AP scan...");
    WiFi.scanNetworks(true);   // non-blocking — returns immediately
    s_scanPending = true;
}

// Returns true if scan is done AND a matching profile was found+connecting.
// Returns false if still scanning OR no match (caller should call beginConnect).
static bool checkAsyncScanResult() {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return false;   // not ready yet

    s_scanPending = false;

    if (n <= 0) {
        Serial.println("[WIFI] Scan complete: no APs found");
        WiFi.scanDelete();
        return false;
    }

    Serial.printf("[WIFI] Scan complete: %d APs found\n", n);
    std::vector<std::pair<int, uint8_t>> matches;  // RSSI, profile_idx

    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        Serial.printf("[WIFI] Scan[%d]: '%s' RSSI=%d\n", i, ssid.c_str(), WiFi.RSSI(i));
        for (uint8_t p = 0; p < configGetWifiProfileCount(); p++) {
            const WifiProfile& profile = configGetWifiProfile(p);
            if (ssid == profile.ssid &&
                strlen(profile.ssid) > 0 &&
                strlen(profile.password) > 0) {
                matches.emplace_back(WiFi.RSSI(i), p);
                Serial.printf("[WIFI] → Match profile #%d: '%s' RSSI=%d\n",
                              p, ssid.c_str(), WiFi.RSSI(i));
                break;
            }
        }
    }
    WiFi.scanDelete();

    if (matches.empty()) {
        Serial.println("[WIFI] No matching profiles in range");
        return false;
    }

    auto best = *std::max_element(matches.begin(), matches.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    uint8_t best_profile = best.second;
    const WifiProfile& profile = configGetWifiProfile(best_profile);
    Serial.printf("[WIFI] Connecting to BEST: '%s' (profile #%d) RSSI=%d\n",
                  profile.ssid, best_profile, best.first);

    WiFi.disconnect(false);
    WiFi.begin(profile.ssid, profile.password);
    enterState(WifiState::CONNECTING);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// wifiInit
// ─────────────────────────────────────────────────────────────────────────────
void wifiInit() {
    WiFi.setAutoReconnect(false);
    WiFi.persistent(false);

    if (configGetApEnabled()) {
        WiFi.mode(WIFI_AP);
        delay(100);
        const char* apPass = configGetApPassword();
        const char* apSsid = configGetApSsid();
        Serial.printf("[WIFI] AP config: SSID='%s' PASS='%s' (len=%d)\n", 
                      apSsid, apPass, strlen(apPass));
        // Use explicit security setting (WIFI_AUTH_WPA2_PSK for WPA2)
        bool success = WiFi.softAP(apSsid, apPass, 1, false, 4);
        if (!success) {
            Serial.printf("[WIFI] ERROR: Failed to start AP! Check password length (8-63 chars, must include non-numeric)\n");
        }
        enterState(WifiState::AP);
        Serial.printf("[WIFI] AP mode %s SSID=%s IP=%s\n",
            success ? "active" : "FAILED",
            apSsid, WiFi.softAPIP().toString().c_str());
    } else {
        WiFi.mode(WIFI_STA);
        delay(100);
        s_retryCount = 0;
        beginConnect();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// wifiApplyConfig
// ─────────────────────────────────────────────────────────────────────────────
void wifiApplyConfig() {
    if (configGetApEnabled()) {
        WiFi.disconnect(true);
        delay(50);
        WiFi.mode(WIFI_AP);
        delay(50);
        const char* apPass = configGetApPassword();
        const char* apSsid = configGetApSsid();
        Serial.printf("[WIFI] AP config: SSID='%s' PASS='%s' (len=%d)\n", 
                      apSsid, apPass, strlen(apPass));
        // Use explicit security setting (WIFI_AUTH_WPA2_PSK for WPA2)
        bool success = WiFi.softAP(apSsid, apPass, 1, false, 4);
        if (!success) {
            Serial.printf("[WIFI] ERROR: Failed to start AP! Check password length (8-63 chars, must include non-numeric)\n");
        }
        enterState(WifiState::AP);
        Serial.printf("[WIFI] Switched to AP %s SSID=%s IP=%s\n",
            success ? "active" : "FAILED",
            apSsid, WiFi.softAPIP().toString().c_str());
    } else {
        WiFi.softAPdisconnect(true);
        delay(50);
        WiFi.mode(WIFI_STA);
        delay(100);
        s_retryCount = 0;
        beginConnect();
        Serial.println("[WIFI] Switched to STA");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// wifiTask
// ─────────────────────────────────────────────────────────────────────────────
void wifiTask() {
    uint32_t now = millis();

    switch (s_state) {
    case WifiState::AP:
        return;

    case WifiState::IDLE:
        beginConnect();
        break;

    case WifiState::CONNECTING: {
        wl_status_t status = WiFi.status();
        if (status == WL_CONNECTED) {
            s_retryCount     = 0;
            s_connectedSince = now;
            s_lastMonitorMs  = now;
            Serial.printf("[WIFI] Connected IP=%s RSSI=%d dBm\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            enterState(WifiState::CONNECTED);
            break;
        }
        if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL ||
            elapsed() > WIFI_CONNECT_TIMEOUT_MS) {
            s_retryCount++;
            Serial.printf("[WIFI] Connect failed (status=%d) retry %d in %lus\n",
                          status, s_retryCount, WIFI_RETRY_INTERVAL_MS / 1000UL);
            WiFi.disconnect(false);
            enterState(WifiState::WAITING_RETRY);
        }
        break;
    }

    case WifiState::CONNECTED:
        if (now - s_lastMonitorMs > WIFI_MONITOR_INTERVAL_MS) {
            s_lastMonitorMs = now;
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WIFI] Link lost, reconnecting...");
                s_retryCount = 0;
                s_scanPending = false;
                beginConnect();
            }
        }
        break;

    case WifiState::WAITING_RETRY:
        if (s_scanPending) {
            // Scan started previously — check if results are ready
            if (checkAsyncScanResult()) break;          // connected to best AP
            if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
                // Scan finished but no match found — fall back to first profile
                beginConnect();
            }
            // else: still scanning, do nothing this tick
            break;
        }
        if (elapsed() > WIFI_RETRY_INTERVAL_MS) {
            startAsyncScan();   // non-blocking, returns immediately
        }
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// API
// ─────────────────────────────────────────────────────────────────────────────
WifiState wifiGetState()    { return s_state; }
bool      wifiIsConnected() { return s_state == WifiState::CONNECTED; }
bool      wifiIsAp()        { return s_state == WifiState::AP; }

String wifiGetIp()   { return wifiIsConnected() ? WiFi.localIP().toString()  : String("--"); }
String wifiGetApIp() { return wifiIsAp()        ? WiFi.softAPIP().toString() : String("--"); }
String wifiGetSsid() { return wifiIsConnected() ? WiFi.SSID()               : String("--"); }

int8_t   wifiGetRssi()   { return wifiIsConnected() ? (int8_t)WiFi.RSSI() : 0; }
uint32_t wifiGetUptime() {
    if (!wifiIsConnected()) return 0;
    return (millis() - s_connectedSince) / 1000;
}

void wifiReconnect() {
    Serial.println("[WIFI] Manual reconnect requested");
    s_retryCount  = 0;
    s_scanPending = false;
    beginConnect();
}

void wifiPrint() {
    static const char* str[] = {"IDLE","CONNECTING","CONNECTED","WAITING_RETRY","AP"};
    String ssid = wifiIsAp() ? configGetApSsid() : wifiGetSsid();
    String ip = wifiIsAp() ? wifiGetApIp() : wifiGetIp();
    Serial.printf("\n[WIFI] %s SSID=%s IP=%s RSSI=%d Up=%lus\n",
                  str[(int)s_state],
                  ssid.c_str(),
                  ip.c_str(),
                  wifiGetRssi(),
                  wifiGetUptime());
}