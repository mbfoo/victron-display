#include "wifi_manager.h"
#include "config_store.h"
#include <WiFi.h>
#include <vector>

static WifiState s_state          = WifiState::IDLE;
static uint32_t  s_stateEnteredMs = 0;
static uint32_t  s_lastMonitorMs  = 0;
static uint32_t  s_connectedSince = 0;
static uint8_t   s_retryCount     = 0;

static void enterState(WifiState next) {
    s_state          = next;
    s_stateEnteredMs = millis();
}
static uint32_t elapsed() { return millis() - s_stateEnteredMs; }

static bool tryConnectBestAvailable();

static void beginConnect() {
    // Try profiles in order until one works
    uint8_t count = configGetWifiProfileCount();
    for (uint8_t p = 0; p < count; p++) {
        const WifiProfile& profile = configGetWifiProfile(p);
        if (strlen(profile.ssid) == 0) continue;
        
        Serial.printf("[WIFI] Fallback connect to profile #%d: '%s' (attempt %d)\n",
                      p, profile.ssid, s_retryCount + 1);
        WiFi.disconnect(false);
        WiFi.begin(profile.ssid, profile.password);
        enterState(WifiState::CONNECTING);
        return;
    }
    Serial.println("[WIFI] No valid profiles for fallback, entering AP mode");
    // Fallback to AP if no profiles
    if (configGetApEnabled()) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(configGetApSsid(), configGetApPassword());
        enterState(WifiState::AP);
    }
}


// ─────────────────────────────────────────────────────────────────────────────
// wifiInit  — cold-boot only, WiFi driver not yet started
// ─────────────────────────────────────────────────────────────────────────────
void wifiInit() {
    WiFi.setAutoReconnect(false);
    WiFi.persistent(false);

    if (configGetApEnabled()) {
        // Start AP — mode must be set BEFORE softAP()
        WiFi.mode(WIFI_AP);
        delay(100);
        WiFi.softAP(configGetApSsid(), configGetApPassword());
        enterState(WifiState::AP);
        Serial.printf("[WIFI] AP mode active  SSID=%s  IP=%s\n",
            configGetApSsid(), WiFi.softAPIP().toString().c_str());
    } else {
        WiFi.mode(WIFI_STA);
        delay(100);
        Serial.printf("[WIFI] STA mode");
        s_retryCount = 0;
        beginConnect();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// wifiApplyConfig  — runtime switch (called from web UI save)
//                    WiFi driver is already running here, so
//                    disconnect calls are safe.
// ─────────────────────────────────────────────────────────────────────────────
void wifiApplyConfig() {
    if (configGetApEnabled()) {
        WiFi.disconnect(true);
        delay(50);
        WiFi.mode(WIFI_AP);
        delay(50);
        WiFi.softAP(configGetApSsid(), configGetApPassword());
        enterState(WifiState::AP);
        Serial.printf("[WIFI] Switched to AP  SSID=%s  IP=%s\n",
            configGetApSsid(), WiFi.softAPIP().toString().c_str());
    } else {
        WiFi.softAPdisconnect(true);
        delay(50);
        WiFi.mode(WIFI_STA);
        delay(100);
        s_retryCount = 0;
        beginConnect();
        Serial.printf("[WIFI] Switched to STA");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// wifiTask
// ─────────────────────────────────────────────────────────────────────────────
void wifiTask() {
    uint32_t now = millis();
    
    switch (s_state) {
    case WifiState::AP:
        return;  // softAP runs autonomously
        
    case WifiState::IDLE:
        beginConnect();
        break;
        
    case WifiState::CONNECTING: {
        wl_status_t status = WiFi.status();
        if (status == WL_CONNECTED) {
            s_retryCount = 0;
            s_connectedSince = now;
            s_lastMonitorMs = now;
            Serial.printf("[WIFI] Connected! IP=%s  RSSI=%d dBm\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
            enterState(WifiState::CONNECTED);
            break;
        }
        
        if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL ||
            elapsed() > WIFI_CONNECT_TIMEOUT_MS) {
            
            s_retryCount++;
            Serial.printf("[WIFI] Connect failed (status=%d) retry %d in %lu s\n",
                          status, s_retryCount, WIFI_RETRY_INTERVAL_MS / 1000UL);
            WiFi.disconnect(false);
            enterState(WifiState::WAITING_RETRY);
            break;
        }
        break;
    }
        
    case WifiState::CONNECTED:
        if (now - s_lastMonitorMs > WIFI_MONITOR_INTERVAL_MS) {
            s_lastMonitorMs = now;
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WIFI] Link lost, reconnecting...");
                s_retryCount = 0;
                beginConnect();
                break;
            }
        }
        break;
        
    case WifiState::WAITING_RETRY:
        if (elapsed() > WIFI_RETRY_INTERVAL_MS) {
            // Scan for best available profile before retrying
            if (tryConnectBestAvailable()) {
                break;  // Found and connecting
            }
            beginConnect();  // Fall back to first profile
        }
        break;
    }
}

static bool tryConnectBestAvailable() {
    int n = WiFi.scanNetworks();
    if (n <= 0) {
        Serial.println("[WIFI] No APs found");
        return false;
    }
    
    std::vector<std::pair<int, uint8_t>> matches;  // RSSI, profile_idx
    Serial.printf("[WIFI] Scanning... %d APs found\n", n);
    
    for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        Serial.printf("[WIFI] Scan[%d]: '%s' RSSI=%d\n", i, ssid.c_str(), WiFi.RSSI(i));
        
        for (uint8_t p = 0; p < configGetWifiProfileCount(); p++) {
            const WifiProfile& profile = configGetWifiProfile(p);
            if (ssid == profile.ssid && strlen(profile.ssid) > 0 && strlen(profile.password) > 0) {
                matches.emplace_back(WiFi.RSSI(i), p);
                Serial.printf("[WIFI] → Match profile #%d: '%s' RSSI=%d\n", p, ssid.c_str(), WiFi.RSSI(i));
                break;
            }
        }
    }
    
    if (matches.empty()) {
        Serial.println("[WIFI] No matching profiles in range");
        return false;
    }
    
    // Find strongest signal
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
    s_retryCount = 0;
    beginConnect();
}

void wifiPrint() {
    static const char* str[] = {"IDLE","CONNECTING","CONNECTED","WAITING_RETRY","AP"};
    Serial.println("─── WIFI ───");
    Serial.printf("  State:   %s\n", str[(int)s_state]);
    Serial.printf("  SSID:    %s\n", wifiGetSsid().c_str());
    Serial.printf("  IP:      %s\n", wifiGetIp().c_str());
    Serial.printf("  RSSI:    %d dBm\n", wifiGetRssi());
    Serial.printf("  Uptime:  %lu s\n", wifiGetUptime());
    Serial.printf("  Retries: %d\n", s_retryCount);
    if (wifiIsAp())
        Serial.printf("  AP IP:   %s\n", wifiGetApIp().c_str());
    Serial.println();
}
