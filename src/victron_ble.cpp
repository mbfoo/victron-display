/*
 * victron_ble.cpp
 *
 * Passive BLE scan for Victron Energy devices.
 * Decrypts Extra Manufacturer Data using AES-128-CTR (mbedTLS).
 *
 * Manufacturer Data layout (Product Advertisement, outer record type 0x10):
 *   raw[0..1]  = Vendor ID 0x02E1 (LE)
 *   raw[2]     = Outer record type = 0x10
 *   raw[3..4]  = Model ID (LE, informational)
 *   raw[5]     = Readout type = 0xA0
 *   raw[6]     = Inner record type (0x01 = Solar Charger)
 *   raw[7..8]  = AES nonce (16-bit LE)
 *   raw[9]     = First byte of AES key (for validation)
 *   raw[10..]  = AES-128-CTR encrypted payload
 *
 * Solar Charger decrypted payload layout (inner record type 0x01):
 *   dec[0]     = device state     (uint8)
 *   dec[1]     = charger error    (uint8)
 *   dec[2..3]  = battery voltage  x0.01V  int16 LE, NA=0x7FFF
 *   dec[4..5]  = battery current  x0.1A   int16 LE, NA=0x7FFF
 *   dec[6..7]  = yield today      x0.01kWh uint16 LE, NA=0xFFFF
 *   dec[8..9]  = PV power         x1W     uint16 LE, NA=0xFFFF
 */

#include "victron_ble.h"
#include "config_store.h"
#include <NimBLEDevice.h>
#include <string.h>
#include <Arduino.h>
#include "mbedtls/aes.h"

// ── Constants ─────────────────────────────────────────────────────────────
static constexpr uint16_t VICTRON_MANUFACTURER_ID  = 0x02E1;
static constexpr uint8_t  OUTER_RECORD_PRODUCT     = 0x10;
static constexpr uint8_t  INNER_RECORD_SOLAR        = 0x01;
static constexpr uint32_t BLE_SCAN_RESTART_MS       = 5000;
static constexpr uint32_t DEVICE_TIMEOUT_MS         = 30000;
static constexpr size_t   MIN_MFR_LEN               = 11;  // 10 header + 1 enc byte

// ── State ─────────────────────────────────────────────────────────────────
static VictronMpptData s_devices[MAX_VICTRON_DEVICES];
static uint8_t         s_deviceCount   = 0;
static uint8_t         s_aesKeys[MAX_VICTRON_DEVICES][16];
static bool            s_keyValid[MAX_VICTRON_DEVICES];
static NimBLEScan*     s_scan          = nullptr;
static uint32_t        s_lastRestartMs = 0;

// ── Helpers ───────────────────────────────────────────────────────────────
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
    out[outLen - 1] = 0;
    for (char* p = out; *p; p++) *p = toupper((unsigned char)*p);
}

// ── AES-128-CTR ───────────────────────────────────────────────────────────
// IV = nonce LE in bytes [0..1], remaining 14 bytes zero
static bool aesDecrypt(const uint8_t* key, uint16_t nonce,
                       const uint8_t* in, uint8_t* out, size_t len) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (mbedtls_aes_setkey_enc(&ctx, key, 128) != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }
    uint8_t iv[16]          = {};
    iv[0]                   = (uint8_t)(nonce & 0xFF);
    iv[1]                   = (uint8_t)((nonce >> 8) & 0xFF);
    uint8_t streamBlock[16] = {};
    size_t  ncOff           = 0;
    int ret = mbedtls_aes_crypt_ctr(&ctx, len, &ncOff, iv, streamBlock, in, out);
    mbedtls_aes_free(&ctx);
    return ret == 0;
}

// ── Solar Charger payload decoder ─────────────────────────────────────────
static void decodeSolar(const uint8_t* dec, size_t decLen, VictronMpptData& out) {
    if (decLen < 8) {
        Serial.printf("[VIC] decodeSolar: payload too short (%u bytes)\n", (unsigned)decLen);
        return;
    }

    uint8_t  state  = dec[0];
    uint8_t  err    = dec[1];
    int16_t  batRaw = (int16_t)((uint16_t)dec[2] | ((uint16_t)dec[3] << 8));
    int16_t  curRaw = (int16_t)((uint16_t)dec[4] | ((uint16_t)dec[5] << 8));
    uint16_t ytRaw  = (uint16_t)dec[6] | ((uint16_t)dec[7] << 8);
    uint16_t pvRaw  = (decLen >= 10)
                      ? ((uint16_t)dec[8] | ((uint16_t)dec[9] << 8))
                      : 0xFFFF;

    out.chargerState     = (VictronChargerState)state;
    out.errorCode        = (VictronErrorCode)err;
    out.batteryVoltage_V = (batRaw == (int16_t)0x7FFF) ? 0.0f : batRaw * 0.01f;
    out.batteryCurrent_A = (curRaw == (int16_t)0x7FFF) ? 0.0f : curRaw * 0.1f;
    out.yieldToday_kWh   = (ytRaw  == 0xFFFF)          ? 0.0f : ytRaw  * 0.01f;
    out.pvPower_W        = (pvRaw  == 0xFFFF)          ? 0.0f : (float)pvRaw;

    // Serial.printf("[VIC]   state=%d err=%d bat=%.2fV cur=%.1fA pv=%.0fW yield=%.2fkWh\n",
    //               state, err,
    //               out.batteryVoltage_V, out.batteryCurrent_A,
    //               out.pvPower_W, out.yieldToday_kWh);
}

// ── NimBLE scan callback ──────────────────────────────────────────────────
class VictronAdvCB : public NimBLEScanCallbacks {
public:
    void onResult(const NimBLEAdvertisedDevice* adv) override {
        if (!adv->haveManufacturerData()) return;

        std::string mfrStr = adv->getManufacturerData();
        size_t mfrLen = mfrStr.size();
        if (mfrLen < MIN_MFR_LEN) return;

        // Safe copy — std::string may contain embedded 0x00 bytes
        uint8_t raw[32] = {};
        size_t  copyLen = (mfrLen < sizeof(raw)) ? mfrLen : sizeof(raw);
        memcpy(raw, mfrStr.data(), copyLen);

        // Vendor ID
        uint16_t vendorId = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
        if (vendorId != VICTRON_MANUFACTURER_ID) return;

        // Outer record type must be 0x10
        if (raw[2] != OUTER_RECORD_PRODUCT) return;

        // Parse header fields
        uint16_t modelId     = (uint16_t)raw[3] | ((uint16_t)raw[4] << 8);
        uint8_t  readoutType = raw[5];
        uint8_t  innerType   = raw[6];
        uint16_t nonce       = (uint16_t)raw[7] | ((uint16_t)raw[8] << 8);
        uint8_t  keyByte     = raw[9];
        const uint8_t* enc   = raw + 10;
        size_t   encLen      = copyLen - 10;

        char macStr[18];
        normalizeMac(adv->getAddress().toString().c_str(), macStr, sizeof(macStr));

        // Serial.printf("[VIC] RX MAC=%-17s model=0x%04X rdout=0x%02X "
        //               "inner=0x%02X nonce=0x%04X keyByte=0x%02X encLen=%u RSSI=%d\n",
        //               macStr, modelId, readoutType, innerType,
        //               nonce, keyByte, (unsigned)encLen, adv->getRSSI());

        // Only handle Solar Charger inner records
        if (innerType != INNER_RECORD_SOLAR) {
            Serial.printf("[VIC]   inner type 0x%02X not handled\n", innerType);
            return;
        }

        // Match MAC against configured devices
        for (uint8_t i = 0; i < s_deviceCount; i++) {
            if (!s_devices[i].mac[0]) continue;
            char cfgMac[18];
            normalizeMac(s_devices[i].mac, cfgMac, sizeof(cfgMac));
            if (strcmp(macStr, cfgMac) != 0) continue;

            if (!s_keyValid[i]) {
                Serial.printf("[VIC]   '%s' matched but key not valid\n", s_devices[i].name);
                return;
            }

            // Validate key byte
            if (keyByte != s_aesKeys[i][0]) {
                Serial.printf("[VIC]   '%s' KEY MISMATCH pkt=0x%02X stored=0x%02X\n",
                              s_devices[i].name, keyByte, s_aesKeys[i][0]);
                return;
            }

            // Serial.printf("[VIC]   '%s' key OK — decrypting\n", s_devices[i].name);

            // Decrypt
            uint8_t dec[16] = {};
            size_t  decLen  = (encLen > 16) ? 16 : encLen;
            if (!aesDecrypt(s_aesKeys[i], nonce, enc, dec, decLen)) {
                Serial.printf("[VIC]   '%s' AES FAILED\n", s_devices[i].name);
                return;
            }

            // Serial.printf("[VIC]   dec[0..9]: "
            //               "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
            //               dec[0],dec[1],dec[2],dec[3],dec[4],
            //               dec[5],dec[6],dec[7],dec[8],dec[9]);

            VictronMpptData tmp = {};
            strncpy(tmp.name, s_devices[i].name, sizeof(tmp.name) - 1);
            strncpy(tmp.mac,  s_devices[i].mac,  sizeof(tmp.mac)  - 1);
            decodeSolar(dec, decLen, tmp);
            tmp.nonce        = nonce;
            tmp.rssi         = adv->getRSSI();
            tmp.valid        = true;
            tmp.lastUpdateMs = millis();
            s_devices[i]     = tmp;
            return;
        }

        Serial.printf("[VIC]   No configured device for MAC=%s keyByte=0x%02X\n",
                      macStr, keyByte);
    }
};
static VictronAdvCB s_advCB;

// ── Config ────────────────────────────────────────────────────────────────
static void loadConfig() {
    s_deviceCount = configGetVictronCount();
    if (s_deviceCount > MAX_VICTRON_DEVICES)
        s_deviceCount = MAX_VICTRON_DEVICES;

    for (uint8_t i = 0; i < s_deviceCount; i++) {
        const VictronDeviceCfg& cfg = configGetVictronDevice(i);
        memset(&s_devices[i], 0, sizeof(s_devices[i]));
        strncpy(s_devices[i].name, cfg.name, sizeof(s_devices[i].name) - 1);
        strncpy(s_devices[i].mac,  cfg.mac,  sizeof(s_devices[i].mac)  - 1);
        s_keyValid[i] = false;

        if (cfg.enabled && strlen(cfg.aesKey) >= 32) {
            s_keyValid[i] = hexToBytes(cfg.aesKey, s_aesKeys[i], 16);
            if (s_keyValid[i])
                Serial.printf("[VIC] Device %d: '%s' MAC=%s key[0]=0x%02X\n",
                              i, cfg.name, cfg.mac, s_aesKeys[i][0]);
            else
                Serial.printf("[VIC] Device %d: '%s' BAD AES key\n", i, cfg.name);
        } else {
            Serial.printf("[VIC] Device %d: '%s' disabled or missing key\n", i, cfg.name);
        }
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

const VictronMpptData* victronBleGetDevices()    { return s_devices; }
uint8_t                victronBleGetDeviceCount() { return s_deviceCount; }

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
        Serial.printf("  [%d] '%s' valid=%d PV=%.0fW Bat=%.2fV %.1fA "
                      "state=%d err=%d yield=%.2fkWh RSSI=%d\n",
                      i, d.name, d.valid,
                      d.pvPower_W, d.batteryVoltage_V, d.batteryCurrent_A,
                      (int)d.chargerState, (int)d.errorCode,
                      d.yieldToday_kWh, d.rssi);
    }
    Serial.printf("  Total PV: %.0f W\n", victronBleGetTotalPvPower());
}