/**
 * display.cpp – LVGL 8.x UI for Victron MPPT Monitor
 * Waveshare ESP32-C6-LCD-1.47 (320 x 172)
 *
 * Tab layout (28 px tab bar -> 144 px content)
 *   SOLAR  : big total-PV number, battery V/A, charger mode, online count
 *   devN   : one tab per Victron device - PV, Bat V/A, Yield, Mode, RSSI
 *   SYS    : WiFi SSID+RSSI, IP, MQTT, Uptime, AP toggle
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

// ---- LVGL setup -------------------------------------------------------------
#define LVGL_BUF_LEN        (LCD_WIDTH * LCD_HEIGHT / 20)
#define LVGL_TICK_PERIOD_MS  5
#define DISPLAY_REFRESH_MS   1000

static lv_disp_draw_buf_t s_drawBuf;
static lv_color_t         s_buf1[LVGL_BUF_LEN];
static lv_color_t         s_buf2[LVGL_BUF_LEN];
static uint32_t           s_lastRefreshMs = 0;
static bool               s_forceRefresh  = false;

// ---- Colour palette ---------------------------------------------------------
#define C(h) lv_color_hex(h)
#define COL_BG     0x0F172A
#define COL_CARD   0x1E293B
#define COL_BORDER 0x334155
#define COL_TEXT   0xCED2D6
#define COL_MUTED  0xCBD5E1
#define COL_ACCENT 0x38BDF8
#define COL_GREEN  0x22C55E
#define COL_ORANGE 0xF59E0B
#define COL_RED    0xEF4444
#define COL_YELLOW 0xEDD609

#define SEL(part, state) ((lv_style_selector_t)((uint32_t)(part) | (uint32_t)(state)))

// ---- Widget handles - SOLAR -------------------------------------------------
static lv_obj_t* s_pvTotal  = nullptr;
static lv_obj_t* s_pvBatV   = nullptr;
static lv_obj_t* s_pvBatA   = nullptr;
static lv_obj_t* s_pvMode   = nullptr;
static lv_obj_t* s_pvOnline = nullptr;

// ---- Widget handles - per-device tabs ---------------------------------------
static lv_obj_t* s_devBadge[MAX_VICTRON_DEVICES] = {};
static lv_obj_t* s_devPv   [MAX_VICTRON_DEVICES] = {};
static lv_obj_t* s_devBatV [MAX_VICTRON_DEVICES] = {};
static lv_obj_t* s_devBatA [MAX_VICTRON_DEVICES] = {};
static lv_obj_t* s_devYield[MAX_VICTRON_DEVICES] = {};
static lv_obj_t* s_devMode [MAX_VICTRON_DEVICES] = {};
static lv_obj_t* s_devRssi [MAX_VICTRON_DEVICES] = {};
static uint8_t   s_devCount = 0;

// ---- Widget handles - SYS ---------------------------------------------------
static lv_obj_t* s_sysWifi   = nullptr;
static lv_obj_t* s_sysIp     = nullptr;
static lv_obj_t* s_sysMqtt   = nullptr;
static lv_obj_t* s_sysUptime = nullptr;
static lv_obj_t* s_swAp      = nullptr;

// ---- LVGL driver callbacks (identical to original) --------------------------

static void lvglFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* map) {
    lcdCaptureFlushTile(area->x1, area->y1, area->x2, area->y2, (const uint16_t*)&map->full);
    lcdAddWindow(area->x1, area->y1, area->x2, area->y2, (uint16_t*)&map->full);
    lv_disp_flush_ready(drv);
}

static void lvglTickCb(void*) { lv_tick_inc(LVGL_TICK_PERIOD_MS); }

// ---- Callbacks --------------------------------------------------------------

static void cbApSwitch(lv_event_t* e) {
    bool on = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    configSetApEnabled(on);
    configSave();
    wifiApplyConfig();
    s_forceRefresh = true;
}

// ---- UI helpers (same as original) ------------------------------------------

static lv_obj_t* makeSettingsRow(lv_obj_t* parent, lv_coord_t y,
                                  lv_coord_t h, const char* label) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_pos(row, 0, y);
    lv_obj_set_size(row, 320, h);
    lv_obj_set_style_bg_color(row, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* lbl = lv_label_create(row);
    lv_obj_set_pos(lbl, 12, (h - 20) / 2);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, C(COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &montserrat_20_1bpp, LV_PART_MAIN);
    return row;
}

static lv_obj_t* makeListRow(lv_obj_t* parent, lv_coord_t y,
                              const char* key, lv_obj_t** outVal) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_pos(row, 0, y);
    lv_obj_set_size(row, 320, 44);
    lv_obj_set_style_bg_color(row, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* keyLbl = lv_label_create(row);
    lv_obj_set_pos(keyLbl, 12, 4);
    lv_label_set_text(keyLbl, key);
    lv_obj_set_style_text_color(keyLbl, C(COL_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_font(keyLbl, &montserrat_12_1bpp, LV_PART_MAIN);
    lv_obj_t* valLbl = lv_label_create(row);
    lv_obj_set_pos(valLbl, 12, 20);
    lv_obj_set_size(valLbl, 296, 22);
    lv_label_set_text(valLbl, "--");
    lv_label_set_long_mode(valLbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(valLbl, C(COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(valLbl, &montserrat_20_1bpp, LV_PART_MAIN);
    *outVal = valLbl;
    return row;
}

static lv_obj_t* makeSwitch(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                             lv_event_cb_t cb) {
    lv_obj_t* sw = lv_switch_create(parent);
    lv_obj_set_pos(sw, x, y);
    lv_obj_set_size(sw, 52, 26);
    lv_obj_set_style_bg_color(sw, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, C(COL_ACCENT), SEL(LV_PART_INDICATOR, LV_STATE_CHECKED));
    lv_obj_set_style_bg_color(sw, C(COL_TEXT), LV_PART_KNOB);
    if (cb) lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return sw;
}

// ---- Build UI ---------------------------------------------------------------

#define ZERO_TAB_PAD(t) \
    lv_obj_set_style_pad_all((t),    0, LV_PART_MAIN); \
    lv_obj_set_style_pad_row((t),    0, LV_PART_MAIN); \
    lv_obj_set_style_pad_column((t), 0, LV_PART_MAIN)

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

    lv_obj_t* bar = lv_tabview_get_tab_btns(tv);
    lv_obj_set_style_bg_color(bar,     C(COL_CARD),   LV_PART_MAIN);
    lv_obj_set_style_border_side(bar,  LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(bar,   C(COL_MUTED),  LV_PART_ITEMS);
    lv_obj_set_style_text_font(bar,    &montserrat_16_1bpp, LV_PART_ITEMS);
    lv_obj_set_style_text_color(bar,   C(COL_ACCENT), SEL(LV_PART_ITEMS, LV_STATE_CHECKED));
    lv_obj_set_style_border_side(bar,  LV_BORDER_SIDE_BOTTOM,
                                 SEL(LV_PART_ITEMS, LV_STATE_CHECKED));
    lv_obj_set_style_border_color(bar, C(COL_ACCENT),
                                  SEL(LV_PART_ITEMS, LV_STATE_CHECKED));
    lv_obj_set_style_border_width(bar, 2, SEL(LV_PART_ITEMS, LV_STATE_CHECKED));

    // =========================================================================
    // Tab 0 - SOLAR (no scroll, fixed 144 px)
    // =========================================================================
    lv_obj_t* tabSolar = lv_tabview_add_tab(tv, "SOLAR");
    lv_obj_set_style_bg_color(tabSolar, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabSolar, LV_OPA_COVER, LV_PART_MAIN);
    ZERO_TAB_PAD(tabSolar);
    lv_obj_clear_flag(tabSolar, LV_OBJ_FLAG_SCROLLABLE);

    // Big total PV number
    s_pvTotal = lv_label_create(tabSolar);
    lv_label_set_text(s_pvTotal, "---");
    lv_obj_set_style_text_font(s_pvTotal, &montserrat_36_1bpp, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_pvTotal, C(COL_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pvTotal, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pvTotal, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_pos(s_pvTotal, 70, 10);

    lv_obj_t* pvUnitLbl = lv_label_create(tabSolar);
    lv_label_set_text(pvUnitLbl, "W  Total PV");
    lv_obj_set_style_text_font(pvUnitLbl, &montserrat_14_1bpp, LV_PART_MAIN);
    lv_obj_set_style_text_color(pvUnitLbl, C(COL_MUTED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(pvUnitLbl, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pvUnitLbl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_pos(pvUnitLbl, 70, 58);

    // Divider
    static lv_point_t divPts[] = {{0, 0}, {316, 0}};
    lv_obj_t* div = lv_line_create(tabSolar);
    lv_line_set_points(div, divPts, 2);
    lv_obj_set_pos(div, 2, 80);
    lv_obj_set_style_line_color(div, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_line_width(div, 1, LV_PART_MAIN);

    // Bottom row: Battery V | Current | Mode | Online
    const char*  bkeys[] = { "Battery V", "Current", "Mode", "Online" };
    lv_obj_t**   bvals[] = { &s_pvBatV, &s_pvBatA, &s_pvMode, &s_pvOnline };
    lv_coord_t   bcols[] = { 4, 84, 168, 262 };
    for (int i = 0; i < 4; i++) {
        lv_obj_t* kl = lv_label_create(tabSolar);
        lv_label_set_text(kl, bkeys[i]);
        lv_obj_set_pos(kl, bcols[i], 86);
        lv_obj_set_style_text_font(kl, &montserrat_12_1bpp, LV_PART_MAIN);
        lv_obj_set_style_text_color(kl, C(COL_MUTED), LV_PART_MAIN);
        lv_obj_set_style_bg_color(kl, C(COL_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(kl, LV_OPA_COVER, LV_PART_MAIN);

        lv_obj_t* vl = lv_label_create(tabSolar);
        lv_label_set_text(vl, "--");
        lv_obj_set_pos(vl, bcols[i], 104);
        lv_obj_set_size(vl, 76, 20);
        lv_label_set_long_mode(vl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(vl, &montserrat_16_1bpp, LV_PART_MAIN);
        lv_obj_set_style_text_color(vl, C(COL_MUTED), LV_PART_MAIN);
        lv_obj_set_style_bg_color(vl, C(COL_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(vl, LV_OPA_COVER, LV_PART_MAIN);
        *bvals[i] = vl;
    }

    // =========================================================================
    // Tabs 1..N - per-device (scrollable)
    // =========================================================================
    for (uint8_t i = 0; i < s_devCount && i < MAX_VICTRON_DEVICES; i++) {
        char tabName[8] = {};
        strncpy(tabName, devs[i].name, 6);
        if (!tabName[0]) snprintf(tabName, sizeof(tabName), "S%d", i + 1);

        lv_obj_t* tab = lv_tabview_add_tab(tv, tabName);
        lv_obj_set_style_bg_color(tab, C(COL_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, LV_PART_MAIN);
        ZERO_TAB_PAD(tab);
        lv_obj_add_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(tab, LV_DIR_VER);

        // Header card: full name + online badge
        lv_obj_t* hdr = lv_obj_create(tab);
        lv_obj_set_pos(hdr, 0, 0);
        lv_obj_set_size(hdr, 320, 30);
        lv_obj_set_style_bg_color(hdr, C(COL_CARD), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
        lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
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
        lv_label_set_text(s_devBadge[i], "-- --");
        lv_obj_set_pos(s_devBadge[i], 228, 9);
        lv_obj_set_size(s_devBadge[i], 84, 16);
        lv_label_set_long_mode(s_devBadge[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(s_devBadge[i], &montserrat_12_1bpp, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_devBadge[i], C(COL_MUTED), LV_PART_MAIN);

        // 6 data rows starting at y=30
        makeListRow(tab,  30, "PV Power",    &s_devPv[i]);
        makeListRow(tab,  74, "Battery V",   &s_devBatV[i]);
        makeListRow(tab, 118, "Battery A",   &s_devBatA[i]);
        makeListRow(tab, 162, "Yield today", &s_devYield[i]);
        makeListRow(tab, 206, "Mode",        &s_devMode[i]);
        makeListRow(tab, 250, "RSSI",        &s_devRssi[i]);
    }

    // =========================================================================
    // Tab SYS (scrollable)
    // =========================================================================
    lv_obj_t* tabSys = lv_tabview_add_tab(tv, "SYS");
    lv_obj_set_style_bg_color(tabSys, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabSys, LV_OPA_COVER, LV_PART_MAIN);
    ZERO_TAB_PAD(tabSys);
    lv_obj_add_flag(tabSys, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(tabSys, LV_DIR_VER);

    makeListRow(tabSys,   0, "WiFi",   &s_sysWifi);
    makeListRow(tabSys,  44, "IP",     &s_sysIp);
    makeListRow(tabSys,  88, "MQTT",   &s_sysMqtt);
    makeListRow(tabSys, 132, "Uptime", &s_sysUptime);

    lv_obj_t* rowAp = makeSettingsRow(tabSys, 176, 54, "WiFi AP mode");
    s_swAp = makeSwitch(rowAp, 260, 14, cbApSwitch);
}

// ---- Charger state helpers --------------------------------------------------

static const char* chargerStateName(int s) {
    switch (s) {
        case 0:   return "Off";
        case 1:   return "Low Pwr";
        case 2:   return "Fault";
        case 3:   return "Bulk";
        case 4:   return "Absorb";
        case 5:   return "Float";
        case 6:   return "Storage";
        case 7:   return "Equalize";
        case 9:   return "Inverting";
        case 11:  return "PwrSupply";
        case 245: return "Starting";
        case 252: return "Ext Ctrl";
        default:  return "Unknown";
    }
}

static lv_color_t stateColor(int s) {
    if (s == 3 || s == 4) return C(COL_ORANGE);
    if (s == 5 || s == 6) return C(COL_GREEN);
    if (s == 2)            return C(COL_RED);
    return C(COL_ACCENT);
}

// ---- Update - SOLAR tab -----------------------------------------------------

static void updateSolarTab() {
    const VictronMpptData* devs = victronBleGetDevices();
    uint8_t cnt     = victronBleGetDeviceCount();
    float   totalPv = victronBleGetTotalPvPower();

    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f", totalPv);
    lv_label_set_text(s_pvTotal, buf);

    float   batV = 0, batA = 0;
    int     bestState = -1;
    uint8_t online = 0;
    for (uint8_t i = 0; i < cnt; i++) {
        if (!devs[i].valid) continue;
        online++;
        if (bestState < 0) {
            bestState = (int)devs[i].chargerState;
            batV      = devs[i].batteryVoltage_V;
            batA      = devs[i].batteryCurrent_A;
        }
    }

    lv_obj_set_style_text_color(s_pvTotal,
        online > 0 ? C(COL_ACCENT) : C(COL_MUTED), LV_PART_MAIN);

    if (online > 0) {
        snprintf(buf, sizeof(buf), "%.2fV", batV);
        lv_label_set_text(s_pvBatV, buf);
        lv_obj_set_style_text_color(s_pvBatV, C(COL_TEXT), LV_PART_MAIN);

        snprintf(buf, sizeof(buf), "%.1fA", batA);
        lv_label_set_text(s_pvBatA, buf);
        lv_obj_set_style_text_color(s_pvBatA, C(COL_TEXT), LV_PART_MAIN);

        lv_label_set_text(s_pvMode, chargerStateName(bestState));
        lv_obj_set_style_text_color(s_pvMode, stateColor(bestState), LV_PART_MAIN);
    } else {
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
        (online > 0)               ? C(COL_ORANGE) : C(COL_RED),
        LV_PART_MAIN);
}

// ---- Update - per-device tabs -----------------------------------------------

static void updateDeviceTabs() {
    const VictronMpptData* devs = victronBleGetDevices();
    char buf[32];

    for (uint8_t i = 0; i < s_devCount && i < MAX_VICTRON_DEVICES; i++) {
        if (!s_devPv[i]) continue;
        const VictronMpptData& d = devs[i];

        if (d.valid) {
            lv_label_set_text(s_devBadge[i], "Online");
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
            lv_obj_set_style_text_color(s_devMode[i], stateColor(cs), LV_PART_MAIN);

            snprintf(buf, sizeof(buf), "%d dBm", d.rssi);
            lv_label_set_text(s_devRssi[i], buf);
            lv_obj_set_style_text_color(s_devRssi[i],
                d.rssi > -70 ? C(COL_GREEN) :
                d.rssi > -85 ? C(COL_ORANGE) : C(COL_RED),
                LV_PART_MAIN);
        } else {
            lv_label_set_text(s_devBadge[i], "Offline");
            lv_obj_set_style_text_color(s_devBadge[i], C(COL_RED), LV_PART_MAIN);

            lv_obj_t* off[] = {
                s_devPv[i], s_devBatV[i], s_devBatA[i],
                s_devYield[i], s_devMode[i], s_devRssi[i]
            };
            for (lv_obj_t* lbl : off) {
                lv_label_set_text(lbl, "--");
                lv_obj_set_style_text_color(lbl, C(COL_MUTED), LV_PART_MAIN);
            }
        }
    }
}

// ---- Update - SYS tab -------------------------------------------------------

static void updateSysTab() {
    char buf[48];

    if (wifiIsConnected()) {
        snprintf(buf, sizeof(buf), "%s  %ddBm", wifiGetSsid().c_str(), wifiGetRssi());
        lv_label_set_text(s_sysWifi, buf);
        lv_obj_set_style_text_color(s_sysWifi, C(COL_GREEN), LV_PART_MAIN);
        lv_label_set_text(s_sysIp, wifiGetIp().c_str());
        lv_obj_set_style_text_color(s_sysIp, C(COL_TEXT), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_sysWifi, "disconnected");
        lv_obj_set_style_text_color(s_sysWifi, C(COL_RED), LV_PART_MAIN);
        lv_label_set_text(s_sysIp, "--");
        lv_obj_set_style_text_color(s_sysIp, C(COL_MUTED), LV_PART_MAIN);
    }

    bool mq = mqttIsConnected();
    lv_label_set_text(s_sysMqtt, mq ? "connected" : "disconnected");
    lv_obj_set_style_text_color(s_sysMqtt, mq ? C(COL_GREEN) : C(COL_MUTED), LV_PART_MAIN);

    uint32_t tot = millis() / 1000UL;
    uint32_t d   = tot / 86400UL; tot %= 86400UL;
    uint32_t h   = tot / 3600UL;  tot %= 3600UL;
    uint32_t m   = tot / 60UL;
    uint32_t s   = tot % 60UL;
    snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
             (unsigned long)d, (unsigned long)h,
             (unsigned long)m, (unsigned long)s);
    lv_label_set_text(s_sysUptime, buf);
    lv_obj_set_style_text_color(s_sysUptime, C(COL_TEXT), LV_PART_MAIN);
}

// ---- Public API (identical signatures to original) --------------------------

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

    const esp_timer_create_args_t ta = {.callback = lvglTickCb, .name = "lvgltick"};
    esp_timer_handle_t t = nullptr;
    esp_err_t err = esp_timer_create(&ta, &t);
    if (err != ESP_OK || t == nullptr) {
        Serial.printf("[DISP] esp_timer_create FAILED: %s\n", esp_err_to_name(err));
        return;
    }
    esp_timer_start_periodic(t, LVGL_TICK_PERIOD_MS * 1000);

    Serial.printf("[DISP] Free heap before buildUi: %lu bytes\n",
                  (unsigned long)ESP.getFreeHeap());
    buildUi();
    Serial.printf("[DISP] Free heap after  buildUi: %lu bytes\n",
                  (unsigned long)ESP.getFreeHeap());

    lcdSetBacklight(configGetBacklight());

    if (s_swAp) {
        configGetApEnabled()
            ? lv_obj_add_state(s_swAp, LV_STATE_CHECKED)
            : lv_obj_clear_state(s_swAp, LV_STATE_CHECKED);
    }

    s_lastRefreshMs = millis();
    Serial.println("[DISP] UI init OK - dark theme");
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
    if (!s_swAp) return;
    lcdSetBacklight(configGetBacklight());
    configGetApEnabled()
        ? lv_obj_add_state(s_swAp, LV_STATE_CHECKED)
        : lv_obj_clear_state(s_swAp, LV_STATE_CHECKED);
    s_forceRefresh = true;
}