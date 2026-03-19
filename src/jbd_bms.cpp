/**
 * jbd_bms.cpp
 *
 * Non-blocking BLE client for JBD/Xiaoxiang BMS.
 * Requires NimBLE-Arduino >= 2.3.0 (ESP32-C6 support).
 */

#include "jbd_bms.h"
#include <NimBLEDevice.h>

// ─── UUIDs ───────────────────────────────────────────────────────────────────
static const char* SVC_UUID   = "ff00";
static const char* CHR_NOTIFY = "ff01";
static const char* CHR_WRITE  = "ff02";

// ─── JBD commands ─────────────────────────────────────────────────────────────
static const uint8_t CMD_BASIC = 0x03;
static const uint8_t CMD_CELLS = 0x04;
static const uint8_t CMD_FET   = 0xE1;

// ─── Internal state ───────────────────────────────────────────────────────────
static BmsData   s_data        = {};
static BmsState  s_state       = BmsState::SCANNING;
static char      s_devName[32] = "JBD-SP";
static uint32_t  s_pollMs      = 5000;

static bool      s_fetPending       = false;
static bool      s_fetChargeReq     = true;
static bool      s_fetDischargeReq  = true;

static uint32_t  s_lastPollMs   = 0;
static uint32_t  s_scanStartMs  = 0;
static uint32_t  s_connectStartMs = 0;
static uint16_t  s_scanResultCount = 0;

// Receive reassembly buffer
static uint8_t   s_rxBuf[128];
static uint8_t   s_rxLen      = 0;
static bool      s_rxActive   = false;

// NimBLE handles
static NimBLEClient*               s_client    = nullptr;
static NimBLERemoteCharacteristic* s_chrWrite  = nullptr;
static NimBLERemoteCharacteristic* s_chrNotify = nullptr;
static NimBLEAddress               s_targetAddr;
static bool                        s_targetFound = false;

// ─── Forward declarations ─────────────────────────────────────────────────────
static void     startScan();
static bool     connectBms();
static void     sendReadCmd(uint8_t cmd);
static void     sendFetCmd(bool chg, bool dsg);
static uint16_t calcChecksum(const uint8_t* data, uint8_t len);
static void     parseBasicInfo(const uint8_t* buf, uint8_t len);
static void     parseCellVoltages(const uint8_t* buf, uint8_t len);
static void     handleNotification(const uint8_t* data, size_t len);

// Set by notify callback to request CMD_CELLS; consumed by bmsTask()
static bool s_requestCells = false;

static uint32_t s_basicReceivedMs = 0;   // when CMD_BASIC response arrived


// ─── Simulation mode ──────────────────────────────────────────────────────────
static bool     s_simEnabled     = false;
static uint32_t s_simLastUpdateMs = 0;
static float    s_simSoc          = 72.0f;   // start at 72 %
static float    s_simCurrent      = -8.5f;   // discharging
static float    s_simDir          = -1.0f;   // -1 = draining, +1 = charging

static void simUpdate() {
    uint32_t now = millis();
    if (now - s_simLastUpdateMs < 2000) return;
    s_simLastUpdateMs = now;

    // Slowly drain or charge, bounce between 10 % and 95 %
    s_simSoc += s_simDir * 0.3f;
    if (s_simSoc <= 10.0f)  { s_simSoc = 10.0f;  s_simDir =  1.0f; }
    if (s_simSoc >= 95.0f)  { s_simSoc = 95.0f;  s_simDir = -1.0f; }

    if (s_simDir < 0) {
        s_simCurrent = -8.5f;
    } else {
        s_simCurrent =  9.2f;
    }

    float packV = 12.0f + (s_simSoc / 100.0f) * 4.2f;   // rough 4S pack

    BmsBasicInfo& b = s_data.basic;
    b.totalVoltage_V    = packV;
    b.current_A         = s_simCurrent;
    b.remainCapacity_Ah = 100.0f * (s_simSoc / 100.0f);
    b.nominalCapacity_Ah= 100.0f;
    b.cycleCount        = 42;
    b.stateOfCharge_pct = (uint8_t)s_simSoc;
    // Respect FET requests so the limiter can be tested
    uint8_t fetSt = 0x00;
    if (s_fetChargeReq)    fetSt |= 0x01;
    if (s_fetDischargeReq) fetSt |= 0x02;
    b.fetStatus = fetSt;

    // If charge FET is off, stop charging current in sim
    if (!s_fetChargeReq && s_simCurrent > 0.0f) {
        s_simCurrent = 0.0f;
        Serial.println("[BMS] [sim] Charge FET OFF — charging current stopped");
    }
    // If discharge FET is off, stop discharge current
    if (!s_fetDischargeReq && s_simCurrent < 0.0f) {
        s_simCurrent = 0.0f;
        Serial.println("[BMS] [sim] Discharge FET OFF — discharge current stopped");
    }
    b.numCells          = 4;
    b.numNTC            = 1;
    b.temperature_C[0]  = 23.5f + sinf(now / 30000.0f) * 2.0f;
    b.protectionStatus  = 0x0000;
    b.valid             = true;

    BmsCellData& c = s_data.cells;
    c.cellCount = 4;
    float baseCell = (packV / 4.0f) * 1000.0f;   // mV per cell
    c.cellVoltage_mV[0] = (uint16_t)(baseCell + 3.0f);
    c.cellVoltage_mV[1] = (uint16_t)(baseCell - 2.0f);
    c.cellVoltage_mV[2] = (uint16_t)(baseCell + 1.0f);
    c.cellVoltage_mV[3] = (uint16_t)(baseCell - 1.0f);
    c.valid = true;

    s_data.lastUpdateMs = now;
}

// ─────────────────────────────────────────────────────────────────────────────
//  BLE Callbacks
// ─────────────────────────────────────────────────────────────────────────────

class BmsScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        s_scanResultCount++;

        // Print every discovered device so the user can verify the target name
        Serial.printf("[BMS] [scan] #%u  name: %-24s  addr: %s  RSSI: %d dBm\n",
                      s_scanResultCount,
                      dev->getName().empty() ? "(no name)" : dev->getName().c_str(),
                      dev->getAddress().toString().c_str(),
                      dev->getRSSI());

        if (s_targetFound) return;

        if (dev->getName().find(s_devName) != std::string::npos) {
            Serial.printf("[BMS] [scan] >>> TARGET FOUND: \"%s\"  addr: %s  RSSI: %d dBm <<<\n",
                          dev->getName().c_str(),
                          dev->getAddress().toString().c_str(),
                          dev->getRSSI());
            s_targetAddr  = dev->getAddress();
            s_targetFound = true;
            NimBLEDevice::getScan()->stop();
        }
    }

    void onScanEnd(const NimBLEScanResults& /*results*/, int reason) override {
        uint32_t elapsed = millis() - s_scanStartMs;
        if (s_targetFound) {
            Serial.printf("[BMS] [scan] Scan stopped after %u ms – target acquired\n", elapsed);
        } else {
            Serial.printf("[BMS] [scan] Scan ended (reason %d) after %u ms, %u devices seen – restarting\n",
                          reason, elapsed, s_scanResultCount);
            // Restart if device not yet found
            if (s_state == BmsState::SCANNING) {
                NimBLEDevice::getScan()->start(0);
                s_scanStartMs     = millis();
                s_scanResultCount = 0;
            }
        }
    }
};

class BmsClientCallbacks : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient* client, int reason) override {
        Serial.printf("[BMS] [connect] Disconnected from %s (reason: %d) after %u ms uptime\n",
                      client->getPeerAddress().toString().c_str(),
                      reason,
                      millis() - s_connectStartMs);
        s_state       = BmsState::SCANNING;
        s_targetFound = false;
        s_chrWrite    = nullptr;
        s_chrNotify   = nullptr;
        s_rxActive    = false;
        s_rxLen       = 0;
        startScan();
    }

    void onConnect(NimBLEClient* client) override {
        Serial.printf("[BMS] [connect] TCP/BLE link established to %s\n",
                      client->getPeerAddress().toString().c_str());
    }
};

static BmsScanCallbacks   s_scanCB;
static BmsClientCallbacks s_clientCB;

static void notifyCB(NimBLERemoteCharacteristic* chr,
                     uint8_t* data, size_t len, bool /*isNotify*/) {
    Serial.printf("[BMS] [notify] %u bytes from %s\n",
                  (unsigned)len, chr->getUUID().toString().c_str());
    handleNotification(data, len);
}


// ─────────────────────────────────────────────────────────────────────────────
//  Scan helper
// ─────────────────────────────────────────────────────────────────────────────

static void startScan() {
    s_scanResultCount = 0;
    s_scanStartMs     = millis();

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_scanCB, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(80);
    scan->start(0);   // 0 ms = scan until stop() is called

    s_state = BmsState::SCANNING;
    Serial.printf("[BMS] [scan] Started active scan, looking for \"%s\"...\n", s_devName);
}


// ─────────────────────────────────────────────────────────────────────────────
//  Connect
// ─────────────────────────────────────────────────────────────────────────────

static bool connectBms() {
    s_state           = BmsState::CONNECTING;
    s_connectStartMs  = millis();

    Serial.printf("[BMS] [connect] Initiating connection to %s...\n",
                  s_targetAddr.toString().c_str());

    if (s_client == nullptr) {
        s_client = NimBLEDevice::createClient();
        s_client->setClientCallbacks(&s_clientCB, false);
        Serial.println("[BMS] [connect] NimBLE client created");
    }

    Serial.println("[BMS] [connect] Calling connect() – may take up to ~5 s...");
    if (!s_client->connect(s_targetAddr)) {
        Serial.printf("[BMS] [connect] Connection FAILED after %u ms – back to scan\n",
                      millis() - s_connectStartMs);
        s_state       = BmsState::DISCONNECTED;
        s_targetFound = false;
        startScan();
        return false;
    }
    Serial.printf("[BMS] [connect] Connected in %u ms\n", millis() - s_connectStartMs);

    // ── Service discovery ──
    Serial.printf("[BMS] [connect] Discovering service 0x%s...\n", SVC_UUID);
    NimBLERemoteService* svc = s_client->getService(SVC_UUID);
    if (!svc) {
        Serial.println("[BMS] [connect] ERROR: Service FF00 not found – disconnecting");
        s_client->disconnect();
        return false;
    }
    Serial.println("[BMS] [connect] Service FF00 found");

    // ── Characteristic discovery ──
    Serial.printf("[BMS] [connect] Getting characteristic FF01 (notify)...\n");
    s_chrNotify = svc->getCharacteristic(CHR_NOTIFY);
    Serial.printf("[BMS] [connect] Getting characteristic FF02 (write)...\n");
    s_chrWrite  = svc->getCharacteristic(CHR_WRITE);

    if (!s_chrNotify) {
        Serial.println("[BMS] [connect] ERROR: Characteristic FF01 not found");
        s_client->disconnect();
        return false;
    }
    if (!s_chrWrite) {
        Serial.println("[BMS] [connect] ERROR: Characteristic FF02 not found");
        s_client->disconnect();
        return false;
    }

    Serial.printf("[BMS] [connect] FF01 properties: notify=%s  indicate=%s\n",
                  s_chrNotify->canNotify()   ? "yes" : "no",
                  s_chrNotify->canIndicate() ? "yes" : "no");
    Serial.printf("[BMS] [connect] FF02 properties: write=%s  writeNoResp=%s\n",
                  s_chrWrite->canWrite()            ? "yes" : "no",
                  s_chrWrite->canWriteNoResponse()  ? "yes" : "no");

    // ── Subscribe ──
    if (s_chrNotify->canNotify()) {
        Serial.println("[BMS] [connect] Subscribing to notifications on FF01...");
        if (!s_chrNotify->subscribe(true, notifyCB)) {
            Serial.println("[BMS] [connect] ERROR: Subscribe failed – disconnecting");
            s_client->disconnect();
            return false;
        }
        Serial.println("[BMS] [connect] Subscribed successfully");
    } else {
        Serial.println("[BMS] [connect] WARNING: FF01 does not support notify");
    }

    s_state = BmsState::CONNECTED;
    Serial.printf("[BMS] [connect] *** BMS ready – polling every %u ms ***\n", s_pollMs);

    if (s_fetPending) {
        Serial.println("[BMS] [connect] Replaying pending FET command...");
        sendFetCmd(s_fetChargeReq, s_fetDischargeReq);
        s_fetPending = false;
    }

    return true;
}


// ─────────────────────────────────────────────────────────────────────────────
//  Protocol helpers
// ─────────────────────────────────────────────────────────────────────────────

static uint16_t calcChecksum(const uint8_t* data, uint8_t len) {
    uint16_t sum = 0;
    for (uint8_t i = 0; i < len; i++) sum += data[i];
    return 0x10000u - sum;
}

static void sendReadCmd(uint8_t cmd) {
    if (!s_chrWrite) return;
    uint8_t payload[2] = { cmd, 0x00 };
    uint16_t chk = calcChecksum(payload, 2);
    uint8_t frame[7] = {
        0xDD, 0xA5, cmd, 0x00,
        (uint8_t)(chk >> 8), (uint8_t)(chk & 0xFF),
        0x77
    };
    Serial.printf("[BMS] [tx] READ cmd=0x%02X  frame: DD A5 %02X 00 %02X %02X 77\n",
                  cmd, cmd, frame[4], frame[5]);
    s_chrWrite->writeValue(frame, sizeof(frame), true);
}

static void sendFetCmd(bool chg, bool dsg) {
    if (!s_chrWrite) {
        Serial.println("[BMS] [fet] Not connected – queuing FET command for later");
        s_fetPending      = true;
        s_fetChargeReq    = chg;
        s_fetDischargeReq = dsg;
        return;
    }
    uint8_t xx = 0x00;
    if (!chg) xx |= 0x01;   // bit0 = charge disabled
    if (!dsg) xx |= 0x02;   // bit1 = discharge disabled
    Serial.printf("[BMS] [fet] Set charge=%s discharge=%s (xx=0x%02X)\n",
                  chg ? "ON" : "OFF", dsg ? "ON" : "OFF", xx);

    // Checksum covers: cmd, len, 0x00, xx
    uint8_t checksumBytes[4] = { 0xE1, 0x02, 0x00, xx };
    uint16_t chk = calcChecksum(checksumBytes, 4);

    uint8_t frame[9] = {
        0xDD, 0x5A, 0xE1, 0x02,
        0x00, xx,
        (uint8_t)(chk >> 8), (uint8_t)(chk & 0xFF),
        0x77
    };
    s_chrWrite->writeValue(frame, sizeof(frame), true);
}



// ─────────────────────────────────────────────────────────────────────────────
//  Parsers
// ─────────────────────────────────────────────────────────────────────────────

static void parseBasicInfo(const uint8_t* buf, uint8_t len) {
    if (len < 7) return;
    if (buf[0] != 0xDD || buf[1] != 0x03 || buf[2] != 0x00) return;

    uint8_t dataLen = buf[3];
    if (len < (uint8_t)(4 + dataLen + 3)) {
        Serial.printf("[BMS] [rx] Basic info frame too short (%u bytes, expected %u)\n",
                      len, 4 + dataLen + 3);
        return;
    }

    const uint8_t* d = buf + 4;
    BmsBasicInfo& b  = s_data.basic;

    b.totalVoltage_V     = (uint16_t)(d[0] << 8 | d[1]) * 0.01f;   // unit: 10mV
    int16_t rawCurrent   = (int16_t)(d[2] << 8 | d[3]);
    b.current_A          = rawCurrent * 0.01f;                       // unit: 10mA
    b.remainCapacity_Ah  = (uint16_t)(d[4] << 8 | d[5]) * 0.01f;   // unit: 10mAh
    b.nominalCapacity_Ah = (uint16_t)(d[6] << 8 | d[7]) * 0.01f;   // unit: 10mAh
    b.cycleCount         = (uint16_t)(d[8] << 8 | d[9]);
    // d[10..11] = production date   (skip)
    // d[12..13] = balance status low  (skip)
    // d[14..15] = balance status high (skip)
    b.protectionStatus   = (uint16_t)(d[16] << 8 | d[17]);          // ← was d[20]
    // d[18]    = software version   (skip)
    b.stateOfCharge_pct  = d[19];                                    // ← was d[23]
    b.fetStatus          = d[20];                                    // ← was d[24]
    b.numCells           = d[21];                                    // ← was d[25]
    b.numNTC             = d[22];                                    // ← was d[26]

    uint8_t ntc = (b.numNTC > 8) ? 8 : b.numNTC;
    for (uint8_t i = 0; i < ntc; i++) {
        uint8_t off = 23 + i * 2;                                    // ← was 27
        if (off + 1 >= dataLen) break;
        uint16_t raw = (uint16_t)(d[off] << 8 | d[off + 1]);
        b.temperature_C[i] = raw * 0.1f - 273.15f;
    }
    b.valid = true;

    Serial.printf("[BMS] [rx] Basic info OK │ %.2fV  %.2fA  SOC:%u%%  "
                  "Rem:%.2fAh  Cyc:%u  FET:0x%02X  Prot:0x%04X  NTC:%u\n",
                  b.totalVoltage_V, b.current_A, b.stateOfCharge_pct,
                  b.remainCapacity_Ah, b.cycleCount,
                  b.fetStatus, b.protectionStatus, b.numNTC);
    for (uint8_t i = 0; i < ntc; i++)
        Serial.printf("[BMS] [rx]   T%u: %.1f °C\n", i, b.temperature_C[i]);
}


static void parseCellVoltages(const uint8_t* buf, uint8_t len) {
    if (len < 7) return;
    if (buf[0] != 0xDD || buf[1] != 0x04 || buf[2] != 0x00) return;

    uint8_t dataLen = buf[3];
    if (len < (uint8_t)(4 + dataLen + 3)) {
        Serial.printf("[BMS] [rx] Cell voltage frame too short (%u bytes, expected %u)\n",
                      len, 4 + dataLen + 3);
        return;
    }

    BmsCellData& c = s_data.cells;
    c.cellCount = dataLen / 2;
    if (c.cellCount > 32) c.cellCount = 32;

    const uint8_t* d = buf + 4;
    uint16_t minMv = 0xFFFF, maxMv = 0;
    for (uint8_t i = 0; i < c.cellCount; i++) {
        c.cellVoltage_mV[i] = (uint16_t)(d[i * 2] << 8 | d[i * 2 + 1]);
        if (c.cellVoltage_mV[i] < minMv) minMv = c.cellVoltage_mV[i];
        if (c.cellVoltage_mV[i] > maxMv) maxMv = c.cellVoltage_mV[i];
    }
    c.valid = true;
    s_data.lastUpdateMs = millis();

    Serial.printf("[BMS] [rx] Cell voltages OK │ %u cells  min:%u mV  max:%u mV  delta:%u mV\n",
                  c.cellCount, minMv, maxMv, maxMv - minMv);
    for (uint8_t i = 0; i < c.cellCount; i++) {
        Serial.printf("[BMS] [rx]   C%02u: %u mV%s\n",
                      i + 1, c.cellVoltage_mV[i],
                      (c.cellVoltage_mV[i] == minMv) ? "  ← min" :
                      (c.cellVoltage_mV[i] == maxMv) ? "  ← max" : "");
    }
}

static void parseFetResponse(const uint8_t* buf, uint8_t len) {
    if (len < 7 || buf[0] != 0xDD || buf[1] != 0xE1 || buf[2] != 0x00) {
        Serial.printf("[BMS] [rx] Invalid FET response frame\n");
        return;
    }

    // FET response format: DD E1 00 02 00 XX CHK1 CHK0 FF 77
    // where XX = new fetStatus (0x01=charge only, 0x02=discharge only, 0x03=both)
    if (len >= 7) {
        s_data.basic.fetStatus = buf[5];  // XX at position 5
        s_data.basic.valid = true;
        Serial.printf("[BMS] [rx] FET response OK: fetStatus=0x%02X (chg=%s, dsg=%s)\n",
                      s_data.basic.fetStatus,
                      (s_data.basic.fetStatus & 0x01) ? "ON" : "OFF",
                      (s_data.basic.fetStatus & 0x02) ? "ON" : "OFF");
    }
}

static void handleNotification(const uint8_t* data, size_t len) {
    if (len == 0) return;

    if (data[0] == 0xDD) {
        if (s_rxLen > 0) {
            Serial.printf("[BMS] [rx] WARNING: discarding incomplete frame (%u bytes)\n", s_rxLen);
        }
        s_rxLen    = 0;
        s_rxActive = true;
    }
    if (!s_rxActive) return;

    if (s_rxLen + len > sizeof(s_rxBuf)) {
        Serial.printf("[BMS] [rx] ERROR: reassembly buffer overflow (%u + %u > %u) – frame dropped\n",
                      s_rxLen, (unsigned)len, (unsigned)sizeof(s_rxBuf));
        s_rxActive = false;
        s_rxLen    = 0;
        return;
    }

    memcpy(s_rxBuf + s_rxLen, data, len);
    s_rxLen += (uint8_t)len;

    Serial.printf("[BMS] [rx] Reassembly: %u/%u bytes (last byte: 0x%02X)\n",
                  s_rxLen, s_rxLen, s_rxBuf[s_rxLen - 1]);

    if (s_rxBuf[s_rxLen - 1] != 0x77) return;  // frame not yet complete

    Serial.printf("[BMS] [rx] Frame complete: %u bytes, cmd=0x%02X\n", s_rxLen, s_rxBuf[1]);
    s_rxActive = false;

    if (s_rxLen < 4) { s_rxLen = 0; return; }

    uint8_t cmd = s_rxBuf[1];
    if (cmd == CMD_BASIC) {
        parseBasicInfo(s_rxBuf, s_rxLen);
        s_basicReceivedMs = millis();
        s_requestCells = true;
    } else if (cmd == CMD_CELLS) {
        parseCellVoltages(s_rxBuf, s_rxLen);
    } else if (cmd == 0xE1) {     // ← ADD THIS
        parseFetResponse(s_rxBuf, s_rxLen);  // ← ADD THIS
        Serial.printf("[BMS] [rx] FET response OK: fetStatus=0x%02X\n", s_data.basic.fetStatus);
    } else {
        Serial.printf("[BMS] [rx] Unknown cmd 0x%02X – frame ignored\n", cmd);
    }

    s_rxLen = 0;
}


// ─────────────────────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────────────────────

void bmsInit(const char* deviceName, uint32_t pollMs) {
    strncpy(s_devName, deviceName, sizeof(s_devName) - 1);
    s_pollMs = pollMs;
    memset(&s_data, 0, sizeof(s_data));

    Serial.printf("[BMS] Init – looking for \"%s\", poll interval %u ms\n",
                  s_devName, s_pollMs);

    NimBLEDevice::init("");
    NimBLEDevice::setPower(9);
    Serial.println("[BMS] NimBLE stack initialised, TX power +9 dBm");

    startScan();
}

void bmsTask() {
    if (s_simEnabled) {
        // ── Simulation mode – bypass all BLE logic ─
        simUpdate();
        return;
    }

    switch (s_state) {

        case BmsState::SCANNING:
            if (s_targetFound) connectBms();
            break;

        case BmsState::CONNECTING:
            break;

        case BmsState::CONNECTED:
            if (!s_client || !s_client->isConnected()) {
                Serial.println("[BMS] [task] Client no longer connected – switching to DISCONNECTED");
                s_state = BmsState::DISCONNECTED;
                break;
            }
            // CMD_CELLS request deferred from notify callback – send now from loop() context
            if (s_requestCells && (millis() - s_basicReceivedMs >= 300)) {
                s_requestCells = false;
                Serial.println("[BMS] [poll] Requesting cell voltages (deferred 300ms)");
                sendReadCmd(CMD_CELLS);
            }
            if ((millis() - s_lastPollMs) >= s_pollMs) {
                s_lastPollMs   = millis();
                s_requestCells = false;   // cancel any pending cell request; fresh cycle starts
                Serial.printf("[BMS] [poll] Requesting basic info (t=%lu ms)\n", s_lastPollMs);
                s_rxLen    = 0;
                s_rxActive = false;
                sendReadCmd(CMD_BASIC);
            }
            break;


        case BmsState::DISCONNECTED:
            Serial.println("[BMS] [task] State DISCONNECTED – restarting scan");
            s_targetFound = false;
            startScan();
            s_state = BmsState::SCANNING;
            break;
    }
}


// ─── Getters ─────────────────────────────────────────────────────────────────

const BmsData& bmsGetData()     { return s_data; }
bool           bmsIsDataValid() { return s_data.basic.valid && s_data.cells.valid; }
BmsState       bmsGetState()    { return s_state; }

float    bmsGetVoltage()               { return s_data.basic.valid ? s_data.basic.totalVoltage_V    : 0.0f; }
float    bmsGetCurrent()               { return s_data.basic.valid ? s_data.basic.current_A         : 0.0f; }
uint8_t  bmsGetSOC()                   { return s_data.basic.valid ? s_data.basic.stateOfCharge_pct : 0;    }
float    bmsGetTemperature(uint8_t idx) {
    if (!s_data.basic.valid || idx >= s_data.basic.numNTC) return 0.0f;
    return s_data.basic.temperature_C[idx];
}
uint16_t bmsGetCellVoltage(uint8_t idx) {
    if (!s_data.cells.valid || idx >= s_data.cells.cellCount) return 0;
    return s_data.cells.cellVoltage_mV[idx];
}

bool bmsIsCharging()     { return s_data.basic.valid && s_data.basic.current_A > 0.0f; }
bool bmsIsDischarging()  { return s_data.basic.valid && s_data.basic.current_A < 0.0f; }
bool bmsChargeFetOn()    { return s_data.basic.valid && (s_data.basic.fetStatus & 0x01); }
bool bmsdischargeFetOn() { return s_data.basic.valid && (s_data.basic.fetStatus & 0x02); }


// ─── FET control ─────────────────────────────────────────────────────────────

void bmsSetChargeFet(bool enable) {
    Serial.printf("[BMS] bmsSetChargeFet(%s) called, current fetStatus=0x%02X\n",
                  enable ? "true" : "false", s_data.basic.fetStatus);

    s_fetChargeReq = enable;
    s_fetDischargeReq = bmsdischargeFetOn();

    if (s_simEnabled) {
        // Apply immediately to sim data — no BLE needed
        if (enable) s_data.basic.fetStatus |=  0x01;
        else        s_data.basic.fetStatus &= ~0x01;
        Serial.printf("[BMS] [sim] Charge FET set %s → fetStatus=0x%02X\n",
                      enable ? "ON" : "OFF", s_data.basic.fetStatus);
        return;
    }

    if (s_state == BmsState::CONNECTED) {
        sendFetCmd(s_fetChargeReq, s_fetDischargeReq);
        Serial.printf("[BMS] FET command queued, pending=%s\n",
                      s_fetPending ? "yes" : "no");
    } else {
        Serial.println("[BMS] [fet] Not connected, queuing charge FET change");
        s_fetPending = true;
    }
}

void bmsSetDischargeFet(bool enable) {
    s_fetChargeReq    = bmsChargeFetOn();
    s_fetDischargeReq = enable;

    if (s_simEnabled) {
        if (enable) s_data.basic.fetStatus |=  0x02;
        else        s_data.basic.fetStatus &= ~0x02;
        Serial.printf("[BMS] [sim] Discharge FET set %s → fetStatus=0x%02X\n",
                      enable ? "ON" : "OFF", s_data.basic.fetStatus);
        return;
    }

    if (s_state == BmsState::CONNECTED) sendFetCmd(s_fetChargeReq, s_fetDischargeReq);
    else { Serial.println("[BMS] [fet] Not connected – queuing discharge FET change"); s_fetPending = true; }
}

void bmsSetFets(bool chargeEnable, bool dischargeEnable) {
    s_fetChargeReq    = chargeEnable;
    s_fetDischargeReq = dischargeEnable;
    if (s_state == BmsState::CONNECTED) sendFetCmd(chargeEnable, dischargeEnable);
    else { Serial.println("[BMS] [fet] Not connected – queuing FET state change"); s_fetPending = true; }
}

void bmsApplyFetState() {
    if (s_state == BmsState::CONNECTED) {
        Serial.println("[BMS] [fet] Re-applying FET state");
        sendFetCmd(s_fetChargeReq, s_fetDischargeReq);
    }
}

void bmsSetSimulation(bool enable) {
    if (enable == s_simEnabled) return;
    s_simEnabled = enable;
    Serial.printf("[BMS] Simulation mode %s\n", enable ? "ON" : "OFF");

    if (enable) {
        // Freeze BLE state so the scan/connect loop stops running
        if (s_state == BmsState::SCANNING || s_state == BmsState::CONNECTING) {
            NimBLEDevice::getScan()->stop();
        }
        s_simLastUpdateMs = 0;   // trigger immediate first update
        simUpdate();
    } else {
        // Clear sim data and restart BLE scan
        memset(&s_data, 0, sizeof(s_data));
        s_targetFound = false;
        startScan();
    }
}

bool bmsIsSimulation() { return s_simEnabled; }