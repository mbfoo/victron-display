/*
 * victron_ble.cpp
 *
 * Passive BLE scan for Victron manufacturer ID 0x02E1.
 * Decrypts Solar Charger (record type 0x01) payload with AES-128-CTR
 * using mbedTLS (built into ESP-IDF, no extra library needed).
 *
 * Bit layout of decrypted Solar Charger payload:
 *   [0]      device state  (8 bits)
 *   [1]      error code    (8 bits)
 *   [2..3]   battery voltage  ×0.01 V  (10 bits)
 *   [3..4]   battery current  ×0.1 A   (10 bits, signed)
 *   [4..5]   PV power         ×1 W     (12 bits)
 *   [6..7]   yield today      ×0.01 kWh (10 bits)
 */

#include "victron_ble.h"
#include "config_store.h"
#include <NimBLEDevice.h>
#include <string.h>
#include <Arduino.h>
#include "mbedtls/aes.h"

static constexpr uint16_t VICTRON_MANUFACTURER_ID = 0x02E1;
static constexpr uint8_t  RECORD_TYPE_SOLAR        = 0x01;
static constexpr uint32_t BLE_SCAN_RESTART_MS      = 5000;
static constexpr uint32_t DEVICE_TIMEOUT_MS        = 30000;

static VictronMpptData s_devices[MAX_VICTRON_DEVICES];
static uint8_t         s_deviceCount = 0;
static uint8_t         s_aesKeys[MAX_VICTRON_DEVICES][16];
static bool            s_keyValid[MAX_VICTRON_DEVICES];
static NimBLEScan*     s_scan          = nullptr;
static uint32_t        s_lastRestartMs = 0;

// ── Hex decode ────────────────────────────────────────────────────────────
static bool hexToBytes(const char* hex, uint8_t* out, size_t len) {
    if (!hex || strlen(hex) < len * 2) return false;
    for (size_t i = 0; i < len; i++) {
        char buf[3] = { hex[i*2], hex[i*2+1], 0 };
        char* end;
        out[i] = (uint8_t)strtoul(buf, &end, 16);
        if (end != buf + 2) return false;
    }
    return true;
}

static void normalizeMac(const char* in, char* out, size_t outLen) {
    strncpy(out, in, outLen - 1);
    out[outLen-1] = 0;
    for (char* p = out; *p; p++) *p = toupper((unsigned char)*p);
}

// ── AES-128-CTR ───────────────────────────────────────────────────────────
// Nonce block: nonce_le in bytes [0..1], rest zero
static bool aesDecrypt(const uint8_t* key, uint16_t nonce,
                       const uint8_t* in, uint8_t* out, size_t len) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (mbedtls_aes_setkey_enc(&ctx, key, 128) != 0) {
        mbedtls_aes_free(&ctx); return false;
    }
    uint8_t nonceBlock[16] = {};
    nonceBlock[0] = (uint8_t)(nonce & 0xFF);
    nonceBlock[1] = (uint8_t)((nonce >> 8) & 0xFF);
    uint8_t streamBlock[16] = {};
    size_t  ncOff = 0;
    int ret = mbedtls_aes_crypt_ctr(&ctx, len, &ncOff, nonceBlock, streamBlock, in, out);
    mbedtls_aes_free(&ctx);
    return ret == 0;
}

// ── Solar Charger record decoder ──────────────────────────────────────────
static bool decodeSolarCharger(const uint8_t* mfrData, size_t mfrLen,
                                const uint8_t* aesKey, VictronMpptData& out) {
    // mfrData = raw+2 (vendor ID stripped by NimBLE)
    // [0]    = record type
    // [1..2] = nonce 16-bit LE
    // [3]    = unknown byte (NOT a reliable key check)
    // [4..]  = encrypted payload
    if (mfrLen < 8) return false;
    if (mfrData[0] != 0x01 && mfrData[0] != 0x02) return false;

    uint16_t nonce     = (uint16_t)mfrData[2] | ((uint16_t)mfrData[3] << 8); // 0x6C6D
    const uint8_t* enc = mfrData + 4;   // starts at 0x5A
    size_t encLen      = mfrLen - 4;


    uint8_t dec[16] = {};
    if (!aesDecrypt(aesKey, nonce, enc, dec, (encLen > 16 ? 16 : encLen)))
        return false;

    Serial.printf("[VIC] nonce=0x%04X dec: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  nonce, dec[0],dec[1],dec[2],dec[3],dec[4],
                  dec[5],dec[6],dec[7],dec[8],dec[9]);

    uint8_t  state  = dec[0];
    uint8_t  err    = dec[1];
    uint16_t batRaw = (uint16_t)dec[2] | ((uint16_t)dec[3] << 8);
    int16_t  curRaw = (int16_t)((uint16_t)dec[4] | ((uint16_t)dec[5] << 8));
    uint16_t ytRaw  = (uint16_t)dec[6] | ((uint16_t)dec[7] << 8);
    uint16_t pvRaw  = (uint16_t)dec[8] | ((uint16_t)dec[9] << 8);

    out.chargerState     = (VictronChargerState)state;
    out.errorCode        = (VictronErrorCode)err;
    out.batteryVoltage_V = (batRaw == 0xFFFF) ? 0.0f : batRaw * 0.01f;
    out.batteryCurrent_A = (curRaw == (int16_t)0x7FFF) ? 0.0f : curRaw * 0.1f;
    out.yieldToday_kWh   = (ytRaw  == 0xFFFF) ? 0.0f : ytRaw  * 0.01f;
    out.pvPower_W        = (pvRaw  == 0xFFFF) ? 0.0f : (float)pvRaw;
    out.nonce            = nonce;

    Serial.printf("[VIC] mfrData[0..7]: %02X %02X %02X %02X %02X %02X %02X %02X\n",
        mfrData[0],mfrData[1],mfrData[2],mfrData[3],mfrData[4],mfrData[5],mfrData[6],mfrData[7]);
    Serial.printf("[VIC] dec[0..9]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
        dec[0],dec[1],dec[2],dec[3],dec[4],dec[5],dec[6],dec[7],dec[8],dec[9]);
    Serial.printf("[VIC] decoded: batVoltage=%.0f, power=%.0f", out.batteryVoltage_V, out.pvPower_W);
    

    return true;
}


// ── NimBLE callback ───────────────────────────────────────────────────────
class VictronAdvCB : public NimBLEScanCallbacks {
public:
    void onResult(const NimBLEAdvertisedDevice* adv) override {
        if (!adv->haveManufacturerData()) return;
        std::string mfrStr = adv->getManufacturerData();
        if (mfrStr.size() < 6) return;

        const uint8_t* raw = (const uint8_t*)mfrStr.data();
        uint16_t mfrId = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
        if (mfrId != VICTRON_MANUFACTURER_ID) return;
        Serial.printf("[VIC] raw[0..7]: %02X %02X %02X %02X %02X %02X %02X %02X\n",
            raw[0],raw[1],raw[2],raw[3],raw[4],raw[5],raw[6],raw[7]);

        char macStr[18];
        normalizeMac(adv->getAddress().toString().c_str(), macStr, sizeof(macStr));

        Serial.printf("[VIC] MAC=%s raw[2..3]: type=0x%02X keybyte=0x%02X\n",
            macStr, raw[2], raw[3]);

        for (uint8_t i = 0; i < s_deviceCount; i++) {
            if (!s_devices[i].mac[0]) continue;
            char cfgMac[18];
            normalizeMac(s_devices[i].mac, cfgMac, sizeof(cfgMac));
            if (strcmp(macStr, cfgMac) != 0) continue;
            if (!s_keyValid[i]) continue;

            const uint8_t* payload = raw + 2;
            size_t          payLen  = mfrStr.size() - 2;
            VictronMpptData tmp = s_devices[i];
            if (decodeSolarCharger(payload, payLen, s_aesKeys[i], tmp)) {
                tmp.valid        = true;
                tmp.lastUpdateMs = millis();
                tmp.rssi         = adv->getRSSI();
                strncpy(tmp.name, s_devices[i].name, sizeof(tmp.name)-1);
                strncpy(tmp.mac,  s_devices[i].mac,  sizeof(tmp.mac)-1);
                s_devices[i] = tmp;
            }
            return;
        }
    }
};
static VictronAdvCB s_advCB;

// ── Load config ───────────────────────────────────────────────────────────
static void loadConfig() {
    s_deviceCount = configGetVictronCount();
    if (s_deviceCount > MAX_VICTRON_DEVICES) s_deviceCount = MAX_VICTRON_DEVICES;
    for (uint8_t i = 0; i < s_deviceCount; i++) {
        const VictronDeviceCfg& cfg = configGetVictronDevice(i);
        memset(&s_devices[i], 0, sizeof(s_devices[i]));
        strncpy(s_devices[i].name, cfg.name, sizeof(s_devices[i].name)-1);
        strncpy(s_devices[i].mac,  cfg.mac,  sizeof(s_devices[i].mac)-1);
        s_keyValid[i] = false;
        if (cfg.enabled && strlen(cfg.aesKey) >= 32) {
            s_keyValid[i] = hexToBytes(cfg.aesKey, s_aesKeys[i], 16);
            if (!s_keyValid[i])
                Serial.printf("[VIC] Bad AES key for '%s'\n", cfg.name);
        }
        Serial.printf("[VIC] Device %d: '%s' MAC=%s key=%s\n",
                      i, cfg.name, cfg.mac, cfg.aesKey);
        Serial.printf("[VIC] Device %d key bytes: %02X %02X %02X %02X\n",
              i, s_aesKeys[i][0], s_aesKeys[i][1], s_aesKeys[i][2], s_aesKeys[i][3]);
    }
}

// ── Public API ────────────────────────────────────────────────────────────
void victronBleInit() {
    loadConfig();
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    s_scan = NimBLEDevice::getScan();
    s_scan->setScanCallbacks(&s_advCB, false);
    s_scan->setActiveScan(false);
    s_scan->setInterval(100);
    s_scan->setWindow(99);
    s_scan->setDuplicateFilter(false);
    s_scan->start(0, false, false);
    Serial.printf("[VIC] Scan started, %d device(s) configured\n", s_deviceCount);
}

void victronBleTask() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < s_deviceCount; i++) {
        if (s_devices[i].valid &&
            (now - s_devices[i].lastUpdateMs) > DEVICE_TIMEOUT_MS) {
            s_devices[i].valid = false;
            Serial.printf("[VIC] '%s' timed out\n", s_devices[i].name);
        }
    }
    if (!NimBLEDevice::getScan()->isScanning()) {
        if (now - s_lastRestartMs > BLE_SCAN_RESTART_MS) {
            s_lastRestartMs = now;
            NimBLEDevice::getScan()->start(0, false, false);
            Serial.println("[VIC] Scan restarted");
        }
    }
}

const VictronMpptData* victronBleGetDevices()     { return s_devices; }
uint8_t                victronBleGetDeviceCount()  { return s_deviceCount; }

float victronBleGetTotalPvPower() {
    float t = 0.0f;
    for (uint8_t i = 0; i < s_deviceCount; i++)
        if (s_devices[i].valid) t += s_devices[i].pvPower_W;
    return t;
}

void victronBleApplyConfig() {
    if (s_scan && s_scan->isScanning()) s_scan->stop();
    loadConfig();
    if (s_scan) s_scan->start(0, false, false);
    Serial.println("[VIC] Config applied, scan restarted");
}

void victronBlePrint() {
    Serial.println("─── VICTRON BLE ───");
    for (uint8_t i = 0; i < s_deviceCount; i++) {
        const VictronMpptData& d = s_devices[i];
        Serial.printf("  [%d] '%s' valid=%d PV=%.0fW Bat=%.2fV %.1fA state=%d\n",
                      i, d.name, d.valid,
                      d.pvPower_W, d.batteryVoltage_V, d.batteryCurrent_A,
                      (int)d.chargerState);
    }
    Serial.printf("  Total PV: %.0f W\n", victronBleGetTotalPvPower());
}
