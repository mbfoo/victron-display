#include "history_sd.h"
#include "history.h"
#include <SPI.h>
#include <SD.h>

// ─── Config ───────────────────────────────────────────────────────────────────
#define SD_SPEED_HZ         4000000UL    // 4 MHz – conservative for shared bus
#define HIST_FILE_PATH      "/bms_hist.bin"
#define HIST_FILE_MAGIC     0xBEEFCAFEUL
#define HIST_FILE_VERSION   3
#define SAVE_INTERVAL_MS    (10UL * 60UL * 1000UL)   // 10 minutes

// ─── File layout ─────────────────────────────────────────────────────────────
// [SDFileHeader]
// For each tier 0..2:
//   [SDTierHeader]
//   count × HistoryPoint   (oldest first, chronological order)

struct __attribute__((packed)) SDFileHeader {
    uint32_t magic;
    uint8_t  version;
    uint8_t  numTiers;
    uint32_t savedTs;       // millis()/1000 at time of save
};

struct __attribute__((packed)) SDTierHeader {
    uint8_t  tier;
    uint16_t count;
    uint32_t intervalSec;   // sanity check — must match compiled config
};

// ─── State ────────────────────────────────────────────────────────────────────
static bool       s_available = false;
static uint32_t   s_lastSaveMs = 0;

// ─────────────────────────────────────────────────────────────────────────────
// Internal
// ─────────────────────────────────────────────────────────────────────────────

static bool sdOpen() {
    return s_available;
}

// ─────────────────────────────────────────────────────────────────────────────
// Save
// ─────────────────────────────────────────────────────────────────────────────

bool historySDSave() {
    if (!s_available) return false;

    // Gather all points to a temporary buffer (static to avoid stack overflow)
    static HistoryPoint tmpBuf[HISTORY_T1_CAPACITY];   // largest tier = 1440

    File f = SD.open(HIST_FILE_PATH, FILE_WRITE);
    if (!f) {
        Serial.println("[HSD] Save FAILED – could not open file");
        return false;
    }

    // ── Header ────────────────────────────────────────────────────────────────
    SDFileHeader hdr;
    hdr.magic    = HIST_FILE_MAGIC;
    hdr.version  = HIST_FILE_VERSION;
    hdr.numTiers = HISTORY_TIER_COUNT;
    hdr.savedTs  = historyGetNewestTs();   // actual last point ts, not boot-relative millis
    f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));

    uint32_t totalPts = 0;

    // ── Tiers ─────────────────────────────────────────────────────────────────
    for (uint8_t t = 0; t < HISTORY_TIER_COUNT; t++) {
        uint16_t count = historyGetPoints(t, tmpBuf, HISTORY_T1_CAPACITY);

        SDTierHeader th;
        th.tier        = t;
        th.count       = count;
        th.intervalSec = (uint32_t)historyGetInterval(t);
        f.write(reinterpret_cast<const uint8_t*>(&th), sizeof(th));
        f.write(reinterpret_cast<const uint8_t*>(tmpBuf),
                count * sizeof(HistoryPoint));
        totalPts += count;
    }

    f.close();

    uint32_t fileSize = sizeof(SDFileHeader)
                      + HISTORY_TIER_COUNT * sizeof(SDTierHeader)
                      + totalPts * sizeof(HistoryPoint);

    Serial.printf("[HSD] Saved  %lu pts  ~%lu B  ts=%lu s\n",
        totalPts, fileSize, hdr.savedTs);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Load
// ─────────────────────────────────────────────────────────────────────────────

static bool historySDLoad() {
    if (!SD.exists(HIST_FILE_PATH)) {
        Serial.println("[HSD] No snapshot file found – starting fresh");
        return false;
    }

    File f = SD.open(HIST_FILE_PATH, FILE_READ);
    if (!f) {
        Serial.println("[HSD] Load FAILED – could not open file");
        return false;
    }

    // ── Validate header ───────────────────────────────────────────────────────
    SDFileHeader hdr;
    if (f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
        Serial.println("[HSD] Load FAILED – truncated header");
        f.close(); return false;
    }
    if (hdr.magic != HIST_FILE_MAGIC || hdr.version != HIST_FILE_VERSION) {
        Serial.printf("[HSD] Load FAILED – bad magic/version (0x%08lX v%u)\n",
            hdr.magic, hdr.version);
        f.close(); return false;
    }
    if (hdr.numTiers != HISTORY_TIER_COUNT) {
        Serial.println("[HSD] Load FAILED – tier count mismatch");
        f.close(); return false;
    }

    Serial.printf("[HSD] Loading snapshot  savedTs=%lu s\n", hdr.savedTs);

    // Static buffer – largest tier is T1 (1440 pts)
    static HistoryPoint tmpBuf[HISTORY_T1_CAPACITY];

    // ── Read each tier ────────────────────────────────────────────────────────
    for (uint8_t t = 0; t < hdr.numTiers; t++) {
        SDTierHeader th;
        if (f.read(reinterpret_cast<uint8_t*>(&th), sizeof(th)) != sizeof(th)) {
            Serial.printf("[HSD] Tier %u header truncated\n", t);
            break;
        }

        // Sanity: interval must match compiled config
        if (th.intervalSec != historyGetInterval(th.tier)) {
            Serial.printf("[HSD] Tier %u interval mismatch (%lu vs %lu) – skip\n",
                th.tier, th.intervalSec, historyGetInterval(th.tier));
            f.seek(f.position() + th.count * sizeof(HistoryPoint));
            continue;
        }

        // Cap at buffer size
        uint16_t toRead = (th.count < HISTORY_T1_CAPACITY)
                        ? th.count : HISTORY_T1_CAPACITY;
        uint32_t bytes  = toRead * sizeof(HistoryPoint);

        if (f.read(reinterpret_cast<uint8_t*>(tmpBuf), bytes) != (int)bytes) {
            Serial.printf("[HSD] Tier %u data truncated\n", th.tier);
            break;
        }

        historyRestorePoints(th.tier, tmpBuf, toRead);
    }

    f.close();

    // New samples must continue after the last saved timestamp,
    // not restart at t=0 and collide with restored points.
    historySetTimeOffset(hdr.savedTs);
    Serial.printf("[HSD] Time offset set to %lu s\n", hdr.savedTs);

    Serial.println("[HSD] Load complete");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

void historySDInit() {
    Serial.println("[HSD] Initialising SD card...");

    // SPI already initialised by lcdInit() with CLK=1, MISO=3, MOSI=2
    // Do NOT call SPI.begin() again — it would silently no-op anyway

    delay(100);   // let card settle after power-on

    if (!SD.begin(SD_PIN_CS, SPI, SD_SPEED_HZ)) {
        Serial.println("[HSD] SD.begin() FAILED – no card or wiring issue");
        Serial.printf("[HSD]   CLK=%d MISO=%d MOSI=%d CS=%d\n",
            SD_PIN_CLK, SD_PIN_MISO, SD_PIN_MOSI, SD_PIN_CS);
        return;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[HSD] No SD card inserted");
        return;
    }

    const char* typeStr = (cardType == CARD_MMC)  ? "MMC"  :
                          (cardType == CARD_SD)   ? "SDSC" :
                          (cardType == CARD_SDHC) ? "SDHC" : "UNKNOWN";
    Serial.printf("[HSD] Card type: %s  size: %llu MB\n",
        typeStr, SD.cardSize() / (1024ULL * 1024ULL));

    s_available  = true;
    s_lastSaveMs = millis();

    historySDLoad();
}



// ─────────────────────────────────────────────────────────────────────────────
// Task
// ─────────────────────────────────────────────────────────────────────────────

void historySDTask() {
    if (!s_available) return;
    if ((millis() - s_lastSaveMs) < SAVE_INTERVAL_MS) return;

    s_lastSaveMs = millis();
    historySDSave();
}

// ─────────────────────────────────────────────────────────────────────────────

bool historySDAvailable() { return s_available; }
