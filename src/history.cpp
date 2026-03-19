#include "history.h"
#include "jbd_bms.h"

// ─── Promotion ratios ─────────────────────────────────────────────────────────
#define T0_TO_T1  (HISTORY_T1_INTERVAL_S / HISTORY_T0_INTERVAL_S)  // 6
#define T1_TO_T2  (HISTORY_T2_INTERVAL_S / HISTORY_T1_INTERVAL_S)  // 10

// ─── Ring buffer ──────────────────────────────────────────────────────────────
struct RingBuf {
    HistoryPoint* buf;
    uint16_t      capacity;
    uint16_t      head;    // index of next write slot
    uint16_t      count;   // valid entries (0..capacity)
};

static HistoryPoint s_t0buf[HISTORY_T0_CAPACITY];
static HistoryPoint s_t1buf[HISTORY_T1_CAPACITY];
static HistoryPoint s_t2buf[HISTORY_T2_CAPACITY];

static RingBuf s_rings[HISTORY_TIER_COUNT];

// ─── Promotion accumulators ───────────────────────────────────────────────────
struct Accum {
    int32_t  sumSoc;
    int32_t  sumCurrent_cA;
    int32_t  sumCell_mV[4];
    uint16_t count;
};

static Accum s_acc01;   // averages tier-0 points → tier-1
static Accum s_acc12;   // averages tier-1 points → tier-2

// ─── Timing ───────────────────────────────────────────────────────────────────
static uint32_t s_lastSampleMs  = 0;
static uint32_t s_sampleCounter = 0;

static uint32_t s_timeOffset = 0;

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

static void ringPush(RingBuf& r, const HistoryPoint& pt) {
    r.buf[r.head] = pt;
    r.head = (r.head + 1) % r.capacity;
    if (r.count < r.capacity) r.count++;
}

// Read n points in chronological order (oldest first) from the end of the buffer
static uint16_t ringReadNewest(const RingBuf& r, HistoryPoint* out, uint16_t n) {
    uint16_t avail = (r.count < n) ? r.count : n;
    for (uint16_t i = 0; i < avail; i++) {
        // slot index: newest is head-1, next-newest is head-2, etc.
        uint32_t raw = (uint32_t)r.head + r.capacity - avail + i;
        out[i] = r.buf[raw % r.capacity];
    }
    return avail;
}

static void accumAdd(Accum& a, const HistoryPoint& pt) {
    a.sumSoc        += pt.soc;
    a.sumCurrent_cA += pt.current_cA;
    for (int i = 0; i < 4; i++) a.sumCell_mV[i] += pt.cell_mV[i];
    a.count++;
}

static HistoryPoint accumAverage(const Accum& a, uint32_t ts) {
    HistoryPoint p = {};
    p.ts           = ts;
    p.soc          = (uint8_t)(a.sumSoc        / a.count);
    p.current_cA   = (int16_t)(a.sumCurrent_cA / a.count);
    for (int i = 0; i < 4; i++)
        p.cell_mV[i] = (uint16_t)(a.sumCell_mV[i] / a.count);
    return p;
}

static void accumReset(Accum& a) { memset(&a, 0, sizeof(a)); }

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

void historyInit() {
    s_rings[0] = { s_t0buf, HISTORY_T0_CAPACITY, 0, 0 };
    s_rings[1] = { s_t1buf, HISTORY_T1_CAPACITY, 0, 0 };
    s_rings[2] = { s_t2buf, HISTORY_T2_CAPACITY, 0, 0 };
    accumReset(s_acc01);
    accumReset(s_acc12);
    s_lastSampleMs  = 0;
    s_sampleCounter = 0;

    uint32_t totalBytes =
        (uint32_t)(HISTORY_T0_CAPACITY + HISTORY_T1_CAPACITY + HISTORY_T2_CAPACITY)
        * sizeof(HistoryPoint);

    Serial.printf("[HIST] Init OK  T0:%u  T1:%u  T2:%u  RAM:%lu bytes\n",
        HISTORY_T0_CAPACITY, HISTORY_T1_CAPACITY,
        HISTORY_T2_CAPACITY, totalBytes);
    Serial.printf("[HIST] Promotion ratios  T0→T1 every %u pts  T1→T2 every %u pts\n",
        (unsigned)T0_TO_T1, (unsigned)T1_TO_T2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Task — call every loop()
// ─────────────────────────────────────────────────────────────────────────────

void historyTask() {
    uint32_t now = millis();
    if (now - s_lastSampleMs < (uint32_t)(HISTORY_T0_INTERVAL_S * 1000)) return;
    s_lastSampleMs = now;

    if (!bmsIsDataValid()) {
        Serial.println("[HIST] Skip – BMS data not valid");
        return;
    }

    // ── Build tier-0 sample ───────────────────────────────────────────────────
    HistoryPoint pt = {};
    pt.ts          = historyNow();
    pt.soc         = bmsGetSOC();
    pt.current_cA  = (int16_t)(bmsGetCurrent() * 100.0f);

    const BmsCellData& cells = bmsGetData().cells;
    for (uint8_t i = 0; i < 4; i++)
        pt.cell_mV[i] = (i < cells.cellCount) ? cells.cellVoltage_mV[i] : 0;
    pt.voltage_mV = (uint16_t)(bmsGetData().basic.totalVoltage_V * 1000.0f);

    // ── Push to tier 0 ────────────────────────────────────────────────────────
    ringPush(s_rings[0], pt);
    s_sampleCounter++;

    // ── Accumulate for tier 1 ─────────────────────────────────────────────────
    accumAdd(s_acc01, pt);
    if (s_acc01.count >= T0_TO_T1) {
        HistoryPoint p1 = accumAverage(s_acc01, pt.ts);
        ringPush(s_rings[1], p1);
        accumReset(s_acc01);

        Serial.printf("[HIST] T0→T1  counts T0:%u T1:%u T2:%u\n",
            s_rings[0].count, s_rings[1].count, s_rings[2].count);

        // ── Accumulate for tier 2 ─────────────────────────────────────────────
        accumAdd(s_acc12, p1);
        if (s_acc12.count >= T1_TO_T2) {
            HistoryPoint p2 = accumAverage(s_acc12, p1.ts);
            ringPush(s_rings[2], p2);
            accumReset(s_acc12);

            Serial.printf("[HIST] T1→T2  T2 count now %u\n", s_rings[2].count);
        }
    }

    // ── Periodic status log every 60 samples (= 600 s = 10 min) ──────────────
    if (s_sampleCounter % 60 == 0) {
        Serial.printf("[HIST] Status  T0:%u/%u  T1:%u/%u  T2:%u/%u"
                      "  acc01:%u/%u  acc12:%u/%u\n",
            s_rings[0].count, HISTORY_T0_CAPACITY,
            s_rings[1].count, HISTORY_T1_CAPACITY,
            s_rings[2].count, HISTORY_T2_CAPACITY,
            s_acc01.count, (unsigned)T0_TO_T1,
            s_acc12.count, (unsigned)T1_TO_T2);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public read API
// ─────────────────────────────────────────────────────────────────────────────

uint16_t historyGetPoints(uint8_t tier, HistoryPoint* outBuf, uint16_t maxCount) {
    if (tier >= HISTORY_TIER_COUNT) return 0;
    const RingBuf& r = s_rings[tier];
    uint16_t n = (r.count < maxCount) ? r.count : maxCount;
    for (uint16_t i = 0; i < n; i++) {
        uint32_t raw = (uint32_t)r.head + r.capacity - r.count + i;
        outBuf[i] = r.buf[raw % r.capacity];
    }
    return n;
}

uint16_t historyGetRange(uint32_t fromSec, uint32_t toSec,
                         HistoryPoint* outBuf, uint16_t maxCount,
                         uint8_t* outTierUsed) {
    // Pick the finest tier that covers the requested window
    uint32_t window = toSec - fromSec;
    uint8_t tier = 2;
    if (window <= (uint32_t)(HISTORY_T0_CAPACITY * HISTORY_T0_INTERVAL_S))
        tier = 0;
    else if (window <= (uint32_t)(HISTORY_T1_CAPACITY * HISTORY_T1_INTERVAL_S))
        tier = 1;

    if (outTierUsed) *outTierUsed = tier;

    const RingBuf& r = s_rings[tier];
    uint16_t written = 0;

    for (uint16_t i = 0; i < r.count && written < maxCount; i++) {
        uint32_t raw = (uint32_t)r.head + r.capacity - r.count + i;
        const HistoryPoint& pt = r.buf[raw % r.capacity];
        if (pt.ts >= fromSec && pt.ts <= toSec)
            outBuf[written++] = pt;
    }
    return written;
}

uint16_t historyGetCount(uint8_t tier) {
    return (tier < HISTORY_TIER_COUNT) ? s_rings[tier].count : 0;
}

uint32_t historyGetInterval(uint8_t tier) {
    static const uint32_t iv[HISTORY_TIER_COUNT] = {
        HISTORY_T0_INTERVAL_S,
        HISTORY_T1_INTERVAL_S,
        HISTORY_T2_INTERVAL_S
    };
    return (tier < HISTORY_TIER_COUNT) ? iv[tier] : 0;
}

uint32_t historyGetNewestTs() {
    // tier 0 always has the most recent point
    if (s_rings[0].count == 0) return 0;
    uint32_t idx = (s_rings[0].head + HISTORY_T0_CAPACITY - 1) % HISTORY_T0_CAPACITY;
    return s_rings[0].buf[idx].ts;
}

uint32_t historyGetOldestTs() {
    // deepest populated tier has the oldest point
    for (int t = HISTORY_TIER_COUNT - 1; t >= 0; t--) {
        if (s_rings[t].count == 0) continue;
        uint32_t raw = (uint32_t)s_rings[t].head
                     + s_rings[t].capacity - s_rings[t].count;
        return s_rings[t].buf[raw % s_rings[t].capacity].ts;
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug
// ─────────────────────────────────────────────────────────────────────────────

void historyPrint() {
    Serial.println("─── HISTORY ───");
    for (uint8_t t = 0; t < HISTORY_TIER_COUNT; t++) {
        Serial.printf("  Tier %u  interval:%us  %u/%u pts\n",
            t, (unsigned)historyGetInterval(t),
            s_rings[t].count, s_rings[t].capacity);

        // show the 3 newest points
        HistoryPoint tmp[3];
        uint16_t got = ringReadNewest(s_rings[t], tmp, 3);
        for (uint16_t i = 0; i < got; i++) {
            Serial.printf("    [newest-%u]  t=%us  soc=%u%%  cur=%.2fA"
              "  cells=[%u,%u,%u,%u]mV  pack=%.3fV\n",
                (unsigned)(got - 1 - i),
                tmp[i].ts, tmp[i].soc,
                tmp[i].current_cA / 100.0f,
                tmp[i].cell_mV[0], tmp[i].cell_mV[1],
                tmp[i].cell_mV[2], tmp[i].cell_mV[3],
                tmp[i].voltage_mV / 1000.0f);
        }
    }
    Serial.printf("  oldest ts: %us  newest ts: %us\n",
        historyGetOldestTs(), historyGetNewestTs());
    Serial.println();
}

const RingBuf_t* historyGetRingBuf(uint8_t tier) {
    // RingBuf and RingBuf_t are the same struct — cast is safe
    if (tier >= HISTORY_TIER_COUNT) return nullptr;
    return reinterpret_cast<const RingBuf_t*>(&s_rings[tier]);
}

void historyRestorePoints(uint8_t tier, const HistoryPoint* pts, uint16_t count) {
    if (tier >= HISTORY_TIER_COUNT) return;
    RingBuf& r = s_rings[tier];
    r.head  = 0;
    r.count = 0;
    uint16_t cap = r.capacity;
    uint16_t n   = (count < cap) ? count : cap;
    for (uint16_t i = 0; i < n; i++) ringPush(r, pts[i]);
    Serial.printf("[HIST] Restored tier %u: %u points\n", tier, r.count);
}

void     historySetTimeOffset(uint32_t offset) { s_timeOffset = offset; }
uint32_t historyGetTimeOffset()                { return s_timeOffset;   }
uint32_t historyNow()                          { return s_timeOffset + millis() / 1000; }