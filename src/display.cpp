/*
 * display.cpp  —  LVGL 8.x UI, Waveshare ESP32-C6-LCD-1.47 (320 × 172 px)
 *
 * Tab layout  (28 px tab bar, 144 px content):
 *   [SOLAR]  total PV power (large), battery V/A, charger mode, online count
 *   [devN…]  one tab per configured Victron device — full detail + offline notice
 *   [SYS]    WiFi SSID/IP/RSSI, AP toggle, MQTT state, uptime
 */

#include "display.h"
#include "lcd_driver.h"
#include "touch_driver.h"
#include "victron_ble.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "config_store.h"
#include <Arduino.h>
#include <lvgl.h>
#include <esp_timer.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// ── LVGL setup ────────────────────────────────────────────────────────────
#define LVGL_BUF_LEN        (LCD_WIDTH * LCD_HEIGHT / 10)
#define LVGL_TICK_PERIOD_MS  5
#define DISPLAY_REFRESH_MS   1000

static lv_disp_draw_buf_t s_drawBuf;
static lv_color_t         s_buf1[LVGL_BUF_LEN];
static lv_color_t         s_buf2[LVGL_BUF_LEN];
static uint32_t           s_lastRefreshMs = 0;
static bool               s_forceRefresh  = false;

// ── Colour palette ────────────────────────────────────────────────────────
#define C(h)         lv_color_hex(h)
#define COL_BG       0x0F172A
#define COL_CARD     0x1E293B
#define COL_BORDER   0x334155
#define COL_TEXT     0xCED2D6
#define COL_MUTED    0x64748B
#define COL_ACCENT   0x38BDF8
#define COL_GREEN    0x22C55E
#define COL_ORANGE   0xF59E0B
#define COL_RED      0xEF4444

#define SEL(part,state) lv_style_selector_t((uint32_t)(part)|(uint32_t)(state))

#define ZERO_TAB_PAD(t) \
    lv_obj_set_style_pad_all(t,    0, LV_PART_MAIN); \
    lv_obj_set_style_pad_row(t,    0, LV_PART_MAIN); \
    lv_obj_set_style_pad_column(t, 0, LV_PART_MAIN)

// ── Widget handles ────────────────────────────────────────────────────────
// SOLAR tab
static lv_obj_t* s_pvTotal   = nullptr;   // big power number
static lv_obj_t* s_pvUnit    = nullptr;   // "W" unit
static lv_obj_t* s_pvMode    = nullptr;   // charger mode label
static lv_obj_t* s_pvBatV    = nullptr;   // battery voltage
static lv_obj_t* s_pvBatA    = nullptr;   // battery current
static lv_obj_t* s_pvOnline  = nullptr;   // "N/N" online count

// Per-device tabs (up to MAX_VICTRON_DEVICES)
static lv_obj_t* s_devBadge [MAX_VICTRON_DEVICES] = {};
static lv_obj_t* s_devPv    [MAX_VICTRON_DEVICES] = {};
static lv_obj_t* s_devBatV  [MAX_VICTRON_DEVICES] = {};
static lv_obj_t* s_devBatA  [MAX_VICTRON_DEVICES] = {};
static lv_obj_t* s_devYield [MAX_VICTRON_DEVICES] = {};
static lv_obj_t* s_devMode  [MAX_VICTRON_DEVICES] = {};
static lv_obj_t* s_devRssi  [MAX_VICTRON_DEVICES] = {};

// SYS tab
static lv_obj_t* s_sysWifi   = nullptr;
static lv_obj_t* s_sysIp     = nullptr;
static lv_obj_t* s_sysMqtt   = nullptr;
static lv_obj_t* s_sysUptime = nullptr;
static lv_obj_t* s_sysApSw   = nullptr;

static uint8_t s_devCount = 0;

// ── LVGL driver callbacks ─────────────────────────────────────────────────
static void lvglFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* map) {
    lcdCaptureFlushTile(area->x1, area->y1, area->x2, area->y2,
                        (const uint16_t*)&map->full);
    lcdAddWindow(area->x1, area->y1, area->x2, area->y2,
                 (uint16_t*)&map->full);
    lv_disp_flush_ready(drv);
}

static void lvglTickCb(void*) { lv_tick_inc(LVGL_TICK_PERIOD_MS); }

// ── Callbacks ─────────────────────────────────────────────────────────────
static void cbApSwitch(lv_event_t* e) {
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    configSetApEnabled(on);
    configSave();
    wifiApplyConfig();
    s_forceRefresh = true;
}

// ── Helpers ───────────────────────────────────────────────────────────────
static const char* chargerStateName(int s) {
    switch (s) {
        case 0:   return "Off";
        case 1:   return "Low Power";
        case 2:   return "Fault";
        case 3:   return "Bulk";
        case 4:   return "Absorption";
        case 5:   return "Float";
        case 6:   return "Storage";
        case 7:   return "Equalize";
        case 9:   return "Inverting";
        case 11:  return "Pwr Supply";
        case 245: return "Starting";
        case 252: return "Ext Ctrl";
        default:  return "Unknown";
    }
}

static lv_color_t stateColor(int s, bool valid) {
    if (!valid)                   return C(COL_MUTED);
    if (s == 3 || s == 4)        return C(COL_ORANGE);  // Bulk / Absorption
    if (s == 5 || s == 6)        return C(COL_GREEN);   // Float / Storage
    if (s == 2)                   return C(COL_RED);     // Fault
    return C(COL_ACCENT);
}

// Standard key/value row: 320 × 36 px, key left, value right
static void makeRow(lv_obj_t* parent, lv_coord_t y,
                    const char* key, lv_obj_t** outVal) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_pos(row, 0, y);
    lv_obj_set_size(row, 320, 36);
    lv_obj_set_style_bg_color(row,     C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row,       LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_side(row,  LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row,      0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* kl = lv_label_create(row);
    lv_obj_set_pos(kl, 12, 10);
    lv_label_set_text(kl, key);
    lv_obj_set_style_text_color(kl, C(COL_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_font(kl, &montserrat_14_1bpp, LV_PART_MAIN);

    lv_obj_t* vl = lv_label_create(row);
    lv_obj_set_pos(vl, 150, 10);
    lv_obj_set_size(vl, 160, 18);
    lv_label_set_text(vl, "--");
    lv_label_set_long_mode(vl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(vl, C(COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(vl, &montserrat_14_1bpp, LV_PART_MAIN);
    *outVal = vl;
}

// ── Build UI ──────────────────────────────────────────────────────────────
static void buildUi() {
    s_devCount = victronBleGetDeviceCount();
    const VictronMpptData* devs = victronBleGetDevices();

    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t* tv = lv_tabview_create(scr, LV_DIR_TOP, 28);
    lv_obj_set_size(tv, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_pos(tv, 0, 0);
    lv_obj_set_style_bg_color(tv, C(COL_BG), LV_PART_MAIN);

    // Tab bar styling
    lv_obj_t* bar = lv_tabview_get_tab_btns(tv);
    lv_obj_set_style_bg_color(bar,     C(COL_CARD),   LV_PART_MAIN);
    lv_obj_set_style_border_side(bar,  LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(bar,   C(COL_MUTED),  LV_PART_ITEMS);
    lv_obj_set_style_text_font(bar,    &montserrat_12_1bpp, LV_PART_ITEMS);
    lv_obj_set_style_text_color(bar,   C(COL_ACCENT), SEL(LV_PART_ITEMS, LV_STATE_CHECKED));
    lv_obj_set_style_border_side(bar,  LV_BORDER_SIDE_BOTTOM,
                                       SEL(LV_PART_ITEMS, LV_STATE_CHECKED));
    lv_obj_set_style_border_color(bar, C(COL_ACCENT),
                                       SEL(LV_PART_ITEMS, LV_STATE_CHECKED));
    lv_obj_set_style_border_width(bar, 2, SEL(LV_PART_ITEMS, LV_STATE_CHECKED));

    // ─────────────────────────────────────────────────────────────────────
    // Tab 0 : SOLAR overview
    // ─────────────────────────────────────────────────────────────────────
    lv_obj_t* tabSolar = lv_tabview_add_tab(tv, "SOLAR");
    lv_obj_set_style_bg_color(tabSolar, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabSolar, LV_OPA_COVER, LV_PART_MAIN);
    ZERO_TAB_PAD(tabSolar);
    lv_obj_clear_flag(tabSolar, LV_OBJ_FLAG_SCROLLABLE);

    // Giant PV number — top half, centred
    s_pvTotal = lv_label_create(tabSolar);
    lv_label_set_text(s_pvTotal, "---");
    lv_obj_set_style_text_font(s_pvTotal, &montserrat_36_1bpp, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_pvTotal, C(COL_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pvTotal, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pvTotal, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(s_pvTotal, LV_ALIGN_TOP_MID, -14, 8);

    s_pvUnit = lv_label_create(tabSolar);
    lv_label_set_text(s_pvUnit, "W");
    lv_obj_set_style_text_font(s_pvUnit, &montserrat_20_1bpp, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_pvUnit, C(COL_MUTED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pvUnit, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pvUnit, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align_to(s_pvUnit, s_pvTotal, LV_ALIGN_OUT_RIGHT_MID, 4, 6);

    lv_obj_t* pvSub = lv_label_create(tabSolar);
    lv_label_set_text(pvSub, "Total PV Power");
    lv_obj_set_style_text_font(pvSub, &montserrat_12_1bpp, LV_PART_MAIN);
    lv_obj_set_style_text_color(pvSub, C(COL_MUTED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(pvSub, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pvSub, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align_to(pvSub, s_pvTotal, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);

    // Divider between top / bottom halves
    static lv_point_t divPts[] = {{0,0},{316,0}};
    lv_obj_t* div = lv_line_create(tabSolar);
    lv_line_set_points(div, divPts, 2);
    lv_obj_set_pos(div, 2, 86);
    lv_obj_set_style_line_color(div, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_line_width(div, 1, LV_PART_MAIN);

    // Bottom row — 4 columns: Battery V | Battery A | Mode | Online
    // Column labels (muted, y=92)
    auto makeBotKey = [&](const char* txt, lv_coord_t x) {
        lv_obj_t* l = lv_label_create(tabSolar);
        lv_label_set_text(l, txt);
        lv_obj_set_pos(l, x, 92);
        lv_obj_set_style_text_font(l, &montserrat_12_1bpp, LV_PART_MAIN);
        lv_obj_set_style_text_color(l, C(COL_MUTED), LV_PART_MAIN);
        lv_obj_set_style_bg_color(l, C(COL_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, LV_PART_MAIN);
    };
    auto makeBotVal = [&](lv_obj_t** out, lv_coord_t x) {
        lv_obj_t* l = lv_label_create(tabSolar);
        lv_label_set_text(l, "--");
        lv_obj_set_pos(l, x, 110);
        lv_obj_set_size(l, 76, 20);
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(l, &montserrat_16_1bpp, LV_PART_MAIN);
        lv_obj_set_style_text_color(l, C(COL_MUTED), LV_PART_MAIN);
        lv_obj_set_style_bg_color(l, C(COL_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, LV_PART_MAIN);
        *out = l;
    };
    makeBotKey("Battery V",    4);
    makeBotKey("Current",     84);
    makeBotKey("Mode",       168);
    makeBotKey("Online",     262);
    makeBotVal(&s_pvBatV,      4);
    makeBotVal(&s_pvBatA,     84);
    makeBotVal(&s_pvMode,    168);
    makeBotVal(&s_pvOnline,  262);

    // ─────────────────────────────────────────────────────────────────────
    // Tabs 1…N : per-device detail
    // ─────────────────────────────────────────────────────────────────────
    for (uint8_t i = 0; i < s_devCount && i < MAX_VICTRON_DEVICES; i++) {
        // Truncate name to 6 chars for tab button
        char tabName[8] = {};
        strncpy(tabName, devs[i].name, 6);
        if (!tabName[0]) snprintf(tabName, sizeof(tabName), "S%d", i + 1);

        lv_obj_t* tab = lv_tabview_add_tab(tv, tabName);
        lv_obj_set_style_bg_color(tab, C(COL_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, LV_PART_MAIN);
        ZERO_TAB_PAD(tab);
        lv_obj_add_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(tab, LV_DIR_VER);

        // Header card: full device name + online badge
        lv_obj_t* hdr = lv_obj_create(tab);
        lv_obj_set_pos(hdr, 0, 0);
        lv_obj_set_size(hdr, 320, 30);
        lv_obj_set_style_bg_color(hdr,     C(COL_CARD),   LV_PART_MAIN);
        lv_obj_set_style_bg_opa(hdr,       LV_OPA_COVER,  LV_PART_MAIN);
        lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
        lv_obj_set_style_border_side(hdr,  LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_set_style_border_color(hdr, C(COL_BORDER), LV_PART_MAIN);
        lv_obj_set_style_border_width(hdr, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(hdr, 0, LV_PART_MAIN);
        lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* nameLbl = lv_label_create(hdr);
        lv_label_set_text(nameLbl, devs[i].name[0] ? devs[i].name : "Device");
        lv_obj_set_pos(nameLbl, 10, 7);
        lv_obj_set_style_text_font(nameLbl, &montserrat_16_1bpp, LV_PART_MAIN);
        lv_obj_set_style_text_color(nameLbl, C(COL_TEXT), LV_PART_MAIN);

        s_devBadge[i] = lv_label_create(hdr);
        lv_label_set_text(s_devBadge[i], "● --");
        lv_obj_set_pos(s_devBadge[i], 228, 9);
        lv_obj_set_size(s_devBadge[i], 84, 16);
        lv_label_set_long_mode(s_devBadge[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(s_devBadge[i], &montserrat_12_1bpp, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_devBadge[i], C(COL_MUTED), LV_PART_MAIN);

        // Data rows (scrollable — 6 rows × 36 px = 216 px total)
        makeRow(tab,  30,  "PV Power",    &s_devPv[i]);
        makeRow(tab,  66,  "Battery V",   &s_devBatV[i]);
        makeRow(tab, 102,  "Battery A",   &s_devBatA[i]);
        makeRow(tab, 138,  "Yield today", &s_devYield[i]);
        makeRow(tab, 174,  "Mode",        &s_devMode[i]);
        makeRow(tab, 210,  "RSSI",        &s_devRssi[i]);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Tab SYS
    // ─────────────────────────────────────────────────────────────────────
    lv_obj_t* tabSys = lv_tabview_add_tab(tv, "SYS");
    lv_obj_set_style_bg_color(tabSys, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabSys, LV_OPA_COVER, LV_PART_MAIN);
    ZERO_TAB_PAD(tabSys);
    lv_obj_add_flag(tabSys, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(tabSys, LV_DIR_VER);

    makeRow(tabSys,   0, "WiFi",   &s_sysWifi);
    makeRow(tabSys,  36, "IP",     &s_sysIp);
    makeRow(tabSys,  72, "MQTT",   &s_sysMqtt);
    makeRow(tabSys, 108, "Uptime", &s_sysUptime);

    // AP toggle row
    lv_obj_t* apRow = lv_obj_create(tabSys);
    lv_obj_set_pos(apRow, 0, 144);
    lv_obj_set_size(apRow, 320, 44);
    lv_obj_set_style_bg_color(apRow, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(apRow, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(apRow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(apRow, 0, LV_PART_MAIN);
    lv_obj_clear_flag(apRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* apKey = lv_label_create(apRow);
    lv_label_set_text(apKey, "WiFi AP Mode");
    lv_obj_set_pos(apKey, 12, 13);
    lv_obj_set_style_text_color(apKey, C(COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(apKey, &montserrat_16_1bpp, LV_PART_MAIN);

    s_sysApSw = lv_switch_create(apRow);
    lv_obj_set_pos(s_sysApSw, 256, 9);
    lv_obj_set_size(s_sysApSw, 52, 26);
    lv_obj_set_style_bg_color(s_sysApSw, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_sysApSw, C(COL_ACCENT),
                               SEL(LV_PART_INDICATOR, LV_STATE_CHECKED));
    lv_obj_set_style_bg_color(s_sysApSw, C(COL_TEXT), LV_PART_KNOB);
    if (configGetApEnabled()) lv_obj_add_state(s_sysApSw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_sysApSw, cbApSwitch, LV_EVENT_VALUE_CHANGED, nullptr);
}

// ── Update functions ──────────────────────────────────────────────────────
static void updateSolarTab() {
    const VictronMpptData* devs = victronBleGetDevices();
    uint8_t cnt = victronBleGetDeviceCount();

    float totalPv = victronBleGetTotalPvPower();
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f", totalPv);
    lv_label_set_text(s_pvTotal, buf);

    // Aggregate: pick first valid device for V/A/mode
    float batV = 0.0f, batA = 0.0f;
    int   bestState = -1;
    uint8_t online = 0;
    for (uint8_t i = 0; i < cnt; i++) {
        if (!devs[i].valid) continue;
        online++;
        if (bestState < 0) {           // first valid wins
            bestState = (int)devs[i].chargerState;
            batV      = devs[i].batteryVoltage_V;
            batA      = devs[i].batteryCurrent_A;
        }
    }

    if (online > 0) {
        lv_obj_set_style_text_color(s_pvTotal, C(COL_ACCENT), LV_PART_MAIN);

        snprintf(buf, sizeof(buf), "%.2fV", batV);
        lv_label_set_text(s_pvBatV, buf);
        lv_obj_set_style_text_color(s_pvBatV, C(COL_TEXT), LV_PART_MAIN);

        snprintf(buf, sizeof(buf), "%.1fA", batA);
        lv_label_set_text(s_pvBatA, buf);
        lv_obj_set_style_text_color(s_pvBatA, C(COL_TEXT), LV_PART_MAIN);

        lv_label_set_text(s_pvMode, chargerStateName(bestState));
        lv_obj_set_style_text_color(s_pvMode, stateColor(bestState, true), LV_PART_MAIN);
    } else {
        lv_obj_set_style_text_color(s_pvTotal, C(COL_MUTED), LV_PART_MAIN);
        lv_label_set_text(s_pvBatV, "--");
        lv_obj_set_style_text_color(s_pvBatV, C(COL_MUTED), LV_PART_MAIN);
        lv_label_set_text(s_pvBatA, "--");
        lv_obj_set_style_text_color(s_pvBatA, C(COL_MUTED), LV_PART_MAIN);
        lv_label_set_text(s_pvMode, "Offline");
        lv_obj_set_style_text_color(s_pvMode, C(COL_MUTED), LV_PART_MAIN);
    }

    snprintf(buf, sizeof(buf), "%d/%d", online, cnt);
    lv_label_set_text(s_pvOnline, buf);
    lv_obj_set_style_text_color(s_pvOnline,
        (online == cnt && cnt > 0) ? C(COL_GREEN) :
        (online > 0)               ? C(COL_ORANGE) :
                                     C(COL_RED),
        LV_PART_MAIN);
}

static void updateDeviceTabs() {
    const VictronMpptData* devs = victronBleGetDevices();
    char buf[32];

    for (uint8_t i = 0; i < s_devCount && i < MAX_VICTRON_DEVICES; i++) {
        if (!s_devPv[i]) continue;
        const VictronMpptData& d = devs[i];

        if (d.valid) {
            lv_label_set_text(s_devBadge[i], "● Online");
            lv_obj_set_style_text_color(s_devBadge[i], C(COL_GREEN), LV_PART_MAIN);

            snprintf(buf, sizeof(buf), "%.0f W", d.pvPower_W);
            lv_label_set_text(s_devPv[i], buf);
            lv_obj_set_style_text_color(s_devPv[i],
                d.pvPower_W > 0 ? C(COL_ACCENT) : C(COL_TEXT), LV_PART_MAIN);

            snprintf(buf, sizeof(buf), "%.2f V", d.batteryVoltage_V);
            lv_label_set_text(s_devBatV[i], buf);
            lv_obj_set_style_text_color(s_devBatV[i], C(COL_TEXT), LV_PART_MAIN);

            snprintf(buf, sizeof(buf), "%.1f A", d.batteryCurrent_A);
            lv_label_set_text(s_devBatA[i], buf);
            lv_obj_set_style_text_color(s_devBatA[i], C(COL_TEXT), LV_PART_MAIN);

            snprintf(buf, sizeof(buf), "%.2f kWh", d.yieldToday_kWh);
            lv_label_set_text(s_devYield[i], buf);
            lv_obj_set_style_text_color(s_devYield[i], C(COL_TEXT), LV_PART_MAIN);

            int cs = (int)d.chargerState;
            lv_label_set_text(s_devMode[i], chargerStateName(cs));
            lv_obj_set_style_text_color(s_devMode[i], stateColor(cs, true), LV_PART_MAIN);

            snprintf(buf, sizeof(buf), "%d dBm", d.rssi);
            lv_label_set_text(s_devRssi[i], buf);
            lv_obj_set_style_text_color(s_devRssi[i],
                d.rssi > -70 ? C(COL_GREEN) :
                d.rssi > -85 ? C(COL_ORANGE) : C(COL_RED),
                LV_PART_MAIN);
        } else {
            lv_label_set_text(s_devBadge[i], "○ Offline");
            lv_obj_set_style_text_color(s_devBadge[i], C(COL_RED), LV_PART_MAIN);

            lv_obj_t* offLabels[] = {
                s_devPv[i], s_devBatV[i], s_devBatA[i],
                s_devYield[i], s_devMode[i], s_devRssi[i]
            };
            for (lv_obj_t* lbl : offLabels) {
                lv_label_set_text(lbl, "--");
                lv_obj_set_style_text_color(lbl, C(COL_MUTED), LV_PART_MAIN);
            }
        }
    }
}

static void updateSysTab() {
    char buf[48];

    // WiFi
    if (wifiIsConnected()) {
        snprintf(buf, sizeof(buf), "%s  %d dBm",
                 wifiGetSsid().c_str(), wifiGetRssi());
        lv_label_set_text(s_sysWifi, buf);
        lv_obj_set_style_text_color(s_sysWifi, C(COL_GREEN), LV_PART_MAIN);
        lv_label_set_text(s_sysIp, wifiGetIp().c_str());
        lv_obj_set_style_text_color(s_sysIp, C(COL_TEXT), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_sysWifi, "Disconnected");
        lv_obj_set_style_text_color(s_sysWifi, C(COL_RED), LV_PART_MAIN);
        lv_label_set_text(s_sysIp, "--");
        lv_obj_set_style_text_color(s_sysIp, C(COL_MUTED), LV_PART_MAIN);
    }

    // MQTT
    bool mq = mqttIsConnected();
    lv_label_set_text(s_sysMqtt, mq ? "Connected" : "Disconnected");
    lv_obj_set_style_text_color(s_sysMqtt,
        mq ? C(COL_GREEN) : C(COL_MUTED), LV_PART_MAIN);

    // Uptime
    uint32_t tot = millis() / 1000UL;
    uint32_t d   = tot / 86400UL; tot %= 86400UL;
    uint32_t h   = tot / 3600UL;  tot %= 3600UL;
    uint32_t m   = tot / 60UL;    uint32_t s = tot % 60UL;
    snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
             (unsigned long)d, (unsigned long)h,
             (unsigned long)m, (unsigned long)s);
    lv_label_set_text(s_sysUptime, buf);
    lv_obj_set_style_text_color(s_sysUptime, C(COL_TEXT), LV_PART_MAIN);
}

// ── Public API ────────────────────────────────────────────────────────────
void displayInit() {
    lcdInit();
    lv_init();

    lv_disp_draw_buf_init(&s_drawBuf, s_buf1, s_buf2, LVGL_BUF_LEN);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res      = LCD_WIDTH;
    dispDrv.ver_res      = LCD_HEIGHT;
    dispDrv.flush_cb     = lvglFlush;
    dispDrv.full_refresh = 1;
    dispDrv.draw_buf     = &s_drawBuf;
    lv_disp_drv_register(&dispDrv);

    touchInit();
    static lv_indev_drv_t indevDrv;
    lv_indev_drv_init(&indevDrv);
    indevDrv.type    = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = lvglTouchRead;
    lv_indev_drv_register(&indevDrv);

    const esp_timer_create_args_t ta = {
        .callback = lvglTickCb,
        .name     = "lvgl_tick"
    };
    esp_timer_handle_t t = nullptr;
    if (esp_timer_create(&ta, &t) == ESP_OK)
        esp_timer_start_periodic(t, LVGL_TICK_PERIOD_MS * 1000);
    else
        Serial.println("[DISP] esp_timer_create FAILED");

    Serial.printf("[DISP] Free heap before buildUi: %lu\n",
                  (unsigned long)ESP.getFreeHeap());
    buildUi();
    Serial.printf("[DISP] Free heap after  buildUi: %lu\n",
                  (unsigned long)ESP.getFreeHeap());

    lcdSetBacklight(configGetBacklight());
    s_lastRefreshMs = millis();
    Serial.println("[DISP] UI init OK");
}

void displayTask() {
    lv_timer_handler();
    uint32_t now = millis();
    if (s_forceRefresh || (now - s_lastRefreshMs >= DISPLAY_REFRESH_MS)) {
        s_lastRefreshMs = now;
        s_forceRefresh  = false;
        updateSolarTab();
        updateDeviceTabs();
        updateSysTab();
    }
}

void displayRefresh() { s_forceRefresh = true; }

void displayApplyConfig() {
    if (!s_sysApSw) return;
    lcdSetBacklight(configGetBacklight());
    if (configGetApEnabled())
        lv_obj_add_state(s_sysApSw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(s_sysApSw, LV_STATE_CHECKED);
    s_forceRefresh = true;
}