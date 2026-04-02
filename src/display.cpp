/**
 * display.cpp - LVGL 8.x UI for Victron MPPT Monitor
 * Waveshare ESP32-C6-LCD-1.47 (320 x 172)
 *
 * Tabs: SOLAR | <dev0> | <dev1> | ... | SYS
 *
 * Changes vs previous version:
 *   - SOLAR: power font enlarged to montserrat_48_1bpp
 *   - Per-device tabs: created from victronBleGetDevices() array at init time
 *   - SYS tab: backlight slider + display-timeout dropdown added
 *   - displayTask: 10 s debounced backlight save (same as original)
 *   - displayInit/displayApplyConfig: restore slider + dropdown on boot
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
static uint32_t           s_lastRefreshMs  = 0;
static bool               s_forceRefresh   = false;

// ─── Widget handles ── DETAIL ─────────────────────────────────────────────────
static lv_obj_t* s_ddDetailDev  = nullptr;
static lv_obj_t* s_dtPv         = nullptr;
static lv_obj_t* s_dtBatV       = nullptr;
static lv_obj_t* s_dtBatA       = nullptr;
static lv_obj_t* s_dtYield      = nullptr;
static lv_obj_t* s_dtMode       = nullptr;
static lv_obj_t* s_dtRssi       = nullptr;
static lv_obj_t* s_dtBadge      = nullptr;
static uint8_t   s_detailSelIdx = 0;

static uint32_t           s_blSavePendingMs = 0;  // 0 = nothing pending

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

#define SEL(part, state) ((lv_style_selector_t)((uint32_t)(part) | (uint32_t)(state)))

// ---- Widget handles - SOLAR -------------------------------------------------
static lv_obj_t* s_pvTotal  = nullptr;  // 48 pt power number
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
static lv_obj_t* s_sysApSsid = nullptr;
static lv_obj_t* s_sysApPass = nullptr;
static lv_obj_t* s_sliderBl  = nullptr;
static lv_obj_t* s_ddTimeout = nullptr;

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

static void cbBacklightSlider(lv_event_t* e) {
    uint8_t val = (uint8_t)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    lcdSetBacklight(val);
    configSetBacklight(val);
    s_blSavePendingMs = millis();  // (re)start 10 s debounce
}

static void cbTimeoutDropdown(lv_event_t* e) {
    // "1 min\n10 min\n1 hour\n24 hours" -> indices 0-3
    static const uint16_t kVals[] = {60, 600, 3600, 65535};
    uint16_t idx = lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e));
    if (idx > 3) idx = 0;
    configSetDisplayTimeout(kVals[idx]);
    configSave();
}

static void cbDetailDevDropdown(lv_event_t* e) {
    s_detailSelIdx = (uint8_t)lv_dropdown_get_selected(
        (lv_obj_t*)lv_event_get_target(e));
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
    // Use config count so tabs exist even before BLE connects
    s_devCount = configGet().victronDeviceCount;

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
    // Power font enlarged: montserrat_36_1bpp
    // Layout: big number top-centre, divider at y=82, 4-col bottom strip
    // =========================================================================
    lv_obj_t* tabSolar = lv_tabview_add_tab(tv, "SOLAR");
    lv_obj_set_style_bg_color(tabSolar, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabSolar, LV_OPA_COVER, LV_PART_MAIN);
    ZERO_TAB_PAD(tabSolar);
    lv_obj_clear_flag(tabSolar, LV_OBJ_FLAG_SCROLLABLE);

    s_pvTotal = lv_label_create(tabSolar);
    lv_label_set_text(s_pvTotal, "---");
    lv_obj_set_style_text_font(s_pvTotal, &montserrat_64_1bpp, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_pvTotal, C(COL_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pvTotal, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_pvTotal, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(s_pvTotal, LV_ALIGN_TOP_MID, -12, 4);

    lv_obj_t* pvUnitLbl = lv_label_create(tabSolar);
    lv_label_set_text(pvUnitLbl, "W  Total PV");
    lv_obj_set_style_text_font(pvUnitLbl, &montserrat_14_1bpp, LV_PART_MAIN);
    lv_obj_set_style_text_color(pvUnitLbl, C(COL_MUTED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(pvUnitLbl, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pvUnitLbl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align_to(pvUnitLbl, s_pvTotal, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    static lv_point_t divPts[] = {{0, 0}, {316, 0}};
    lv_obj_t* div = lv_line_create(tabSolar);
    lv_line_set_points(div, divPts, 2);
    lv_obj_set_pos(div, 2, 95);
    lv_obj_set_style_line_color(div, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_line_width(div, 1, LV_PART_MAIN);

    // Bottom 4-column strip: Battery V | Current | Mode | Online
    const char*  bkeys[] = { "Battery V", "Current", "Mode", "Online" };
    lv_obj_t**   bvals[] = { &s_pvBatV, &s_pvBatA, &s_pvMode, &s_pvOnline };
    lv_coord_t   bcols[] = { 4, 84, 168, 262 };
    for (int i = 0; i < 4; i++) {
        lv_obj_t* kl = lv_label_create(tabSolar);
        lv_label_set_text(kl, bkeys[i]);
        lv_obj_set_pos(kl, bcols[i], 101);
        lv_obj_set_style_text_font(kl, &montserrat_12_1bpp, LV_PART_MAIN);
        lv_obj_set_style_text_color(kl, C(COL_MUTED), LV_PART_MAIN);
        lv_obj_set_style_bg_color(kl, C(COL_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(kl, LV_OPA_COVER, LV_PART_MAIN);

        lv_obj_t* vl = lv_label_create(tabSolar);
        lv_label_set_text(vl, "--");
        lv_obj_set_pos(vl, bcols[i], 118);
        lv_obj_set_size(vl, 76, 20);
        lv_label_set_long_mode(vl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(vl, &montserrat_16_1bpp, LV_PART_MAIN);
        lv_obj_set_style_text_color(vl, C(COL_MUTED), LV_PART_MAIN);
        lv_obj_set_style_bg_color(vl, C(COL_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(vl, LV_OPA_COVER, LV_PART_MAIN);
        *bvals[i] = vl;
    }


    // =========================================================================
    // Tab SYS (scrollable) - WiFi, IP, MQTT, Uptime, AP toggle,
    //                        Backlight slider, Display timeout dropdown
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

    // AP toggle row
    lv_obj_t* rowAp = makeSettingsRow(tabSys, 176, 54, "WiFi AP mode");
    s_swAp = makeSwitch(rowAp, 260, 14, cbApSwitch);

    // AP credentials info row (only meaningful when AP is enabled)
    lv_obj_t* rowApCreds = lv_obj_create(tabSys);
    lv_obj_set_pos(rowApCreds, 0, 230);
    lv_obj_set_size(rowApCreds, 320, 44);
    lv_obj_set_style_bg_color(rowApCreds, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rowApCreds, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(rowApCreds, 0, LV_PART_MAIN);
    lv_obj_set_style_border_side(rowApCreds, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(rowApCreds, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(rowApCreds, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rowApCreds, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rowApCreds, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* apSsidKey = lv_label_create(rowApCreds);
    lv_obj_set_pos(apSsidKey, 12, 4);
    lv_label_set_text(apSsidKey, "AP SSID / Password");
    lv_obj_set_style_text_color(apSsidKey, C(COL_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_font(apSsidKey, &montserrat_12_1bpp, LV_PART_MAIN);

    s_sysApSsid = lv_label_create(rowApCreds);
    lv_obj_set_pos(s_sysApSsid, 12, 20);
    lv_obj_set_size(s_sysApSsid, 180, 22);
    lv_label_set_long_mode(s_sysApSsid, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_sysApSsid, "--");
    lv_obj_set_style_text_color(s_sysApSsid, C(COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_sysApSsid, &montserrat_16_1bpp, LV_PART_MAIN);

    s_sysApPass = lv_label_create(rowApCreds);
    lv_obj_set_pos(s_sysApPass, 200, 20);
    lv_obj_set_size(s_sysApPass, 116, 22);
    lv_label_set_long_mode(s_sysApPass, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_sysApPass, "--");
    lv_obj_set_style_text_color(s_sysApPass, C(COL_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_sysApPass, &montserrat_16_1bpp, LV_PART_MAIN);


    // Backlight slider row (identical to original SET tab row)
    lv_obj_t* rowBl = makeSettingsRow(tabSys, 274, 54, "Backlight");
    s_sliderBl = lv_slider_create(rowBl);
    lv_obj_set_pos(s_sliderBl, 136, 17);
    lv_obj_set_size(s_sliderBl, 150, 20);
    lv_slider_set_range(s_sliderBl, 10, 100);
    lv_slider_set_value(s_sliderBl, 100, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_sliderBl, C(COL_BORDER),  LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_sliderBl, C(COL_ACCENT),  LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_sliderBl, C(COL_TEXT),    LV_PART_KNOB);
    lv_obj_set_style_border_color(s_sliderBl, C(COL_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_border_width(s_sliderBl, 2, LV_PART_KNOB);
    lv_obj_add_event_cb(s_sliderBl, cbBacklightSlider, LV_EVENT_VALUE_CHANGED, nullptr);

    // Display timeout dropdown row
    lv_obj_t* rowTo = makeSettingsRow(tabSys, 328, 54, "Screen off");
    s_ddTimeout = lv_dropdown_create(rowTo);
    lv_obj_set_pos(s_ddTimeout, 180, 10);
    lv_obj_set_size(s_ddTimeout, 132, 40);
    lv_dropdown_set_options(s_ddTimeout, "1 min\n10 min\n1 hour\n24 hours");
    lv_dropdown_set_selected(s_ddTimeout, 1);  // default 10 min
    lv_obj_set_style_bg_color(s_ddTimeout,     C(COL_CARD),   LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ddTimeout,       LV_OPA_COVER,  LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ddTimeout,   C(COL_TEXT),   LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ddTimeout, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_add_event_cb(s_ddTimeout, cbTimeoutDropdown, LV_EVENT_VALUE_CHANGED, nullptr);

    // ══════════════════════════════════════════════════════════════════════
    // Tab – DETAIL
    // ══════════════════════════════════════════════════════════════════════
    lv_obj_t* tabDetail = lv_tabview_add_tab(tv, "DETAIL");
    lv_obj_set_style_bg_color(tabDetail, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabDetail, LV_OPA_COVER, LV_PART_MAIN);
    ZERO_TAB_PAD(tabDetail);
    lv_obj_add_flag(tabDetail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(tabDetail, LV_DIR_VER);

    // Build dropdown option string from config
    {
        char opts[MAX_VICTRON_DEVICES * (VICTRON_NAME_LEN + 1) + 4] = {};
        uint8_t ndev = configGet().victronDeviceCount;
        if (ndev == 0) {
            strncpy(opts, "No devices", sizeof(opts) - 1);
        } else {
            for (uint8_t i = 0; i < ndev; i++) {
                const char* nm = configGetVictronDevice(i).name;
                if (i > 0) strncat(opts, "\n", sizeof(opts) - strlen(opts) - 1);
                if (nm && nm[0]) strncat(opts, nm, sizeof(opts) - strlen(opts) - 1);
                else { char t[8]; snprintf(t, sizeof(t), "Dev%d", i+1);
                       strncat(opts, t, sizeof(opts) - strlen(opts) - 1); }
            }
        }
        lv_obj_t* hdr = makeSettingsRow(tabDetail, 0, 44, "Device");
        s_ddDetailDev = lv_dropdown_create(hdr);
        lv_obj_set_pos(s_ddDetailDev, 112, 6);
        lv_obj_set_size(s_ddDetailDev, 200, 36);
        lv_dropdown_set_options(s_ddDetailDev, opts);
        lv_dropdown_set_selected(s_ddDetailDev, 0);
        lv_obj_set_style_bg_color(s_ddDetailDev,     C(COL_CARD),   LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_ddDetailDev,       LV_OPA_COVER,  LV_PART_MAIN);
        lv_obj_set_style_text_color(s_ddDetailDev,   C(COL_TEXT),   LV_PART_MAIN);
        lv_obj_set_style_border_color(s_ddDetailDev, C(COL_BORDER), LV_PART_MAIN);
        lv_obj_add_event_cb(s_ddDetailDev, cbDetailDevDropdown,
                            LV_EVENT_VALUE_CHANGED, nullptr);
    }

    s_dtBadge = lv_label_create(tabDetail);
    lv_obj_set_pos(s_dtBadge, 240, 12);
    lv_label_set_text(s_dtBadge, "");

    makeListRow(tabDetail,  44, "PV Power",    &s_dtPv);
    makeListRow(tabDetail,  88, "Battery V",   &s_dtBatV);
    makeListRow(tabDetail, 132, "Battery A",   &s_dtBatA);
    makeListRow(tabDetail, 176, "Yield today", &s_dtYield);
    makeListRow(tabDetail, 220, "Mode",        &s_dtMode);
    makeListRow(tabDetail, 264, "RSSI",        &s_dtRssi);
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
        // Match by index; BLE device array is also indexed by config slot
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

    if (s_sysApSsid && s_sysApPass) {
        const char* apSsid = configGetApSsid();
        const char* apPass = configGetApPassword();
        lv_label_set_text(s_sysApSsid,
            (apSsid && apSsid[0]) ? apSsid : "(not set)");
        lv_label_set_text(s_sysApPass,
            (apPass && apPass[0]) ? apPass : "(no pw)");
        lv_color_t c = configGetApEnabled() ? C(COL_TEXT) : C(COL_MUTED);
        lv_obj_set_style_text_color(s_sysApSsid, c, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_sysApPass, c, LV_PART_MAIN);
    }
}

static void updateDetailTab() {
    if (!s_dtPv) return;
    char buf[32];
    const VictronMpptData* devs = victronBleGetDevices();
    uint8_t idx = s_detailSelIdx;
    if (idx >= victronBleGetDeviceCount()) {
        lv_label_set_text(s_dtBadge, "No device");
        lv_label_set_text(s_dtPv, "--"); lv_label_set_text(s_dtBatV, "--");
        lv_label_set_text(s_dtBatA, "--"); lv_label_set_text(s_dtYield, "--");
        lv_label_set_text(s_dtMode, "--"); lv_label_set_text(s_dtRssi, "--");
        return;
    }
    const VictronMpptData& d = devs[idx];
    bool online = d.valid && (millis() - d.lastUpdateMs < 15000);

    lv_label_set_text(s_dtBadge, online ? "Online" : "Offline");
    lv_obj_set_style_text_color(s_dtBadge,
        online ? C(COL_GREEN) : C(COL_RED), LV_PART_MAIN);

    if (online) {
        snprintf(buf, sizeof(buf), "%.0f W", d.pvPower_W);
        lv_label_set_text(s_dtPv, buf);
        lv_obj_set_style_text_color(s_dtPv,
            d.pvPower_W > 0 ? C(COL_ACCENT) : C(COL_MUTED), LV_PART_MAIN);

        snprintf(buf, sizeof(buf), "%.2f V", d.batteryVoltage_V);
        lv_label_set_text(s_dtBatV, buf);
        lv_obj_set_style_text_color(s_dtBatV, C(COL_TEXT), LV_PART_MAIN);

        snprintf(buf, sizeof(buf), "%.2f A", d.batteryCurrent_A);
        lv_label_set_text(s_dtBatA, buf);
        lv_obj_set_style_text_color(s_dtBatA, C(COL_TEXT), LV_PART_MAIN);

        snprintf(buf, sizeof(buf), "%.2f Wh", d.yieldToday_kWh * 1000.0f);
        lv_label_set_text(s_dtYield, buf);
        lv_obj_set_style_text_color(s_dtYield, C(COL_TEXT), LV_PART_MAIN);

        lv_label_set_text(s_dtMode, chargerStateName((int)d.chargerState));
        lv_obj_set_style_text_color(s_dtMode, stateColor((int)d.chargerState), LV_PART_MAIN);;

        snprintf(buf, sizeof(buf), "%d dBm", d.rssi);
        lv_label_set_text(s_dtRssi, buf);
        lv_obj_set_style_text_color(s_dtRssi,
            d.rssi > -70 ? C(COL_GREEN) : d.rssi > -85 ?
            C(COL_ORANGE) : C(COL_RED), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_dtPv, "--"); lv_label_set_text(s_dtBatV, "--");
        lv_label_set_text(s_dtBatA, "--"); lv_label_set_text(s_dtYield, "--");
        lv_label_set_text(s_dtMode, "--"); lv_label_set_text(s_dtRssi, "--");
    }
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

    // Restore settings from config
    uint8_t bl = configGetBacklight();
    lcdSetBacklight(bl);
    if (s_sliderBl) lv_slider_set_value(s_sliderBl, bl, LV_ANIM_OFF);

    if (s_ddTimeout) {
        static const uint16_t kVals[] = {60, 600, 3600, 65535};
        uint16_t stored = configGetDisplayTimeout();
        uint8_t idx = 1;  // default 10 min
        for (uint8_t i = 0; i < 4; i++) {
            if (kVals[i] == stored) { idx = i; break; }
        }
        lv_dropdown_set_selected(s_ddTimeout, idx);
    }

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

    // Deferred backlight EEPROM save - 10 s after last slider movement
    if (s_blSavePendingMs != 0 && (millis() - s_blSavePendingMs) >= 10000UL) {
        s_blSavePendingMs = 0;
        configSave();
        Serial.println("[DISP] Backlight saved to EEPROM");
    }

    uint32_t now = millis();
    if (s_forceRefresh || (now - s_lastRefreshMs >= DISPLAY_REFRESH_MS)) {
        s_lastRefreshMs = now;
        s_forceRefresh  = false;
        updateSolarTab();
        updateDeviceTabs();
        updateSysTab();
        updateDetailTab();
    }
}

void displayRefresh() { s_forceRefresh = true; }

void displayApplyConfig() {
    if (!s_swAp || !s_sliderBl || !s_ddTimeout) return;

    lcdSetBacklight(configGetBacklight());
    lv_slider_set_value(s_sliderBl, configGetBacklight(), LV_ANIM_OFF);

    static const uint16_t kVals[] = {60, 600, 3600, 65535};
    uint16_t stored = configGetDisplayTimeout();
    uint8_t idx = 1;
    for (uint8_t i = 0; i < 4; i++) {
        if (kVals[i] == stored) { idx = i; break; }
    }
    lv_dropdown_set_selected(s_ddTimeout, idx);

    configGetApEnabled()
        ? lv_obj_add_state(s_swAp, LV_STATE_CHECKED)
        : lv_obj_clear_state(s_swAp, LV_STATE_CHECKED);

    s_forceRefresh = true;
}