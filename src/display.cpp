/**
 * display.cpp  –  LVGL 8.x redesigned UI
 * Waveshare ESP32-C6-LCD-1.47  (320 × 172)
 *
 * Tab layout  (28 px header → 144 px content area)
 * ┌──────────────────────────────────────────────────┐
 * │  MAIN  │  PACK  │  INFO  │  SET                 │  ← 28 px tab bar
 * ├──────────────────────────────────────────────────┤
 * │  Big SOC arc  │  Current + Time left             │  ← MAIN
 * │  BMS / WiFi / MQTT status strip at bottom        │
 * ├──────────────────────────────────────────────────┤
 * │  2-col grid: Voltage, Power, Remain, Temp,       │  ← PACK
 * │              Cells, Cycles, FETs, Limiter        │
 * ├──────────────────────────────────────────────────┤
 * │  2-col grid: IP, WiFi, MQTT, Uptime,             │  ← INFO
 * │              BMS state, BMS name, Publishes, SOC │
 * ├──────────────────────────────────────────────────┤
 * │  Limiter toggle + dropdown                       │  ← SET
 * │  Charge FET / Discharge FET toggles              │
 * │  Backlight slider (fits inside 320 px)           │
 * └──────────────────────────────────────────────────┘
 */

#include "display.h"
#include "lcd_driver.h"
#include "touch_driver.h"
#include "jbd_bms.h"
#include "battery_estimator.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "soc_limiter.h"
#include "config_store.h"

#include <Arduino.h>
#include <lvgl.h>
#include <esp_timer.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// ─── LVGL setup ───────────────────────────────────────────────────────────────
#define LVGL_BUF_LEN        (LCD_WIDTH * LCD_HEIGHT / 20)
#define LVGL_TICK_PERIOD_MS 5
#define DISPLAY_REFRESH_MS  1000

static lv_disp_draw_buf_t s_drawBuf;
static lv_color_t         s_buf1[LVGL_BUF_LEN];
static lv_color_t         s_buf2[LVGL_BUF_LEN];
static uint32_t           s_lastRefreshMs = 0;
static bool               s_forceRefresh  = false;

// ─── Colour palette ───────────────────────────────────────────────────────────
#define C(h)        lv_color_hex(h)
#define COL_BG      0x0F172A
#define COL_CARD    0x1E293B
#define COL_BORDER  0x334155
#define COL_TEXT    0xCED2D6
#define COL_MUTED   0xCBD5E1
#define COL_ACCENT  0x38BDF8
#define COL_GREEN   0x22C55E
#define COL_ORANGE  0xF59E0B
#define COL_RED     0xEF4444
#define COL_YELLOW  0xEDD609

#define SEL(part, state) ((lv_style_selector_t)((uint32_t)(part) | (uint32_t)(state)))

// ─── Widget handles ── MAIN ───────────────────────────────────────────────────
static lv_obj_t* s_arcSoc        = nullptr;
static lv_obj_t* s_lblSocVal     = nullptr;
static lv_obj_t* s_lblPowerVal = nullptr;
static lv_obj_t* s_lblCurrentDir = nullptr;
static lv_obj_t* s_lblTimeLabel  = nullptr;
static lv_obj_t* s_lblTimeValue  = nullptr;
// status strip
static lv_obj_t* s_dotBms        = nullptr;
static lv_obj_t* s_lblBmsStrip   = nullptr;


// ─── Widget handles ── PACK ───────────────────────────────────────────────────
static lv_obj_t* s_pkVoltage  = nullptr;
static lv_obj_t* s_pkPower    = nullptr;
static lv_obj_t* s_pkRemain   = nullptr;
static lv_obj_t* s_pkTemp     = nullptr;
static lv_obj_t* s_pkCells    = nullptr;
static lv_obj_t* s_pkCycles   = nullptr;
static lv_obj_t* s_pkCell[4]      = {nullptr, nullptr, nullptr, nullptr};
static lv_obj_t* s_pkChargeFet    = nullptr;
static lv_obj_t* s_pkDischargeFet = nullptr;
static lv_obj_t* s_pkLimiter  = nullptr;

// ─── Widget handles ── INFO ───────────────────────────────────────────────────
static lv_obj_t* s_infoIp       = nullptr;
static lv_obj_t* s_infoWifi     = nullptr;
static lv_obj_t* s_infoMqtt     = nullptr;
static lv_obj_t* s_infoUptime   = nullptr;
static lv_obj_t* s_infoBms      = nullptr;
static lv_obj_t* s_infoBmsName  = nullptr;
static lv_obj_t* s_infoPub      = nullptr;
static lv_obj_t* s_infoSocLim   = nullptr;

// ─── Widget handles ── SET ────────────────────────────────────────────────────
static lv_obj_t* s_swLimiter    = nullptr;
static lv_obj_t* s_ddLimiterSoc = nullptr;
static lv_obj_t* s_swCharge     = nullptr;
static lv_obj_t* s_swDischarge  = nullptr;
static lv_obj_t* s_sliderBl     = nullptr;
static lv_obj_t* s_swSim = nullptr;
static lv_obj_t* s_ddTimeout  = nullptr;

// SET tab
static lv_obj_t* s_swAp         = nullptr;
// INFO tab
static lv_obj_t* s_infoWifiMode = nullptr;
static lv_obj_t* s_infoApSsid   = nullptr;
static lv_obj_t* s_infoApPass = nullptr;
static lv_obj_t* s_infoApIp     = nullptr;


static uint32_t s_blSavePendingMs = 0;   // 0 = nothing pending

static bool s_suppressSwitchEvents = false; // suppress fet switch events


// ─────────────────────────────────────────────────────────────────────────────
// LVGL driver callbacks  (identical to original)
// ─────────────────────────────────────────────────────────────────────────────

static void lvglFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* map) {
    lcdCaptureFlushTile(area->x1, area->y1, area->x2, area->y2, (const uint16_t*)&map->full);
    lcdAddWindow(area->x1, area->y1, area->x2, area->y2, (uint16_t*)&map->full);
    lv_disp_flush_ready(drv);
}

static void lvglTickCb(void*) { lv_tick_inc(LVGL_TICK_PERIOD_MS); }

// ─────────────────────────────────────────────────────────────────────────────
// Settings callbacks
// ─────────────────────────────────────────────────────────────────────────────

static void cbLimiterSwitch(lv_event_t* e) {
    bool on = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    socLimiterSetEnabled(on);
}

static void cbLimiterSocDropdown(lv_event_t* e) {
    static const uint8_t kVals[] = {100, 90, 80, 70, 60, 50};
    uint16_t idx = lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e));
    if (idx >= 5) idx = 0;
    configSetMaxChargeSoc(kVals[idx]);
    configSave();
    socLimiterApplyConfig();
}

static void cbChargeSwitch(lv_event_t* e) {
    if (s_suppressSwitchEvents) return;
    bool target_on = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    Serial.printf("[DISP] cbChargeSwitch: target=%s\n", target_on ? "ON" : "OFF");
    socLimiterManualFetOverride();
    bmsSetChargeFet(target_on);
}



static void cbDischargeSwitch(lv_event_t* e) {
    if (s_suppressSwitchEvents) return;
    bmsSetDischargeFet(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}

static void cbBacklightSlider(lv_event_t* e) {
    uint8_t val = (uint8_t)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    lcdSetBacklight(val);
    configSetBacklight(val);          // update RAM immediately
    s_blSavePendingMs = millis();     // (re)start the 10 s debounce timer
}

static void cbTimeoutDropdown(lv_event_t* e) {
    // Options: "Never\n1 min\n10 min\n1 hour\n24 hours" → indices 0-4
    static const uint16_t kVals[] = {0, 60, 600, 3600, 65535};
    uint16_t idx = lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e));
    if (idx > 4) idx = 0;
    configSetDisplayTimeout(kVals[idx]);
    configSave();
}

static void cbApSwitch(lv_event_t* e) {
    bool on = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    configSetApEnabled(on);
    configSave();
    wifiApplyConfig();
    s_forceRefresh = true;
}

static void cbSimSwitch(lv_event_t* e) {
    bool on = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
    bmsSetSimulation(on);
}


// ─────────────────────────────────────────────────────────────────────────────
// UI helpers  (same API as original)
// ─────────────────────────────────────────────────────────────────────────────

static lv_obj_t* makeCard(lv_obj_t* parent,
                          lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, C(COL_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(c, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(c, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c, 0, LV_PART_MAIN);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t* makeInfoCell(lv_obj_t* parent,
                              lv_coord_t x, lv_coord_t y, lv_coord_t w,
                              const char* key, lv_obj_t** outVal) {
    lv_obj_t* keyLbl = lv_label_create(parent);
    lv_obj_set_pos(keyLbl, x, y);
    lv_obj_set_size(keyLbl, w, 18);
    lv_label_set_text(keyLbl, key);
    lv_obj_set_style_text_color(keyLbl, C(COL_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_font(keyLbl, &montserrat_12_1bpp, LV_PART_MAIN);

    lv_obj_t* valLbl = lv_label_create(parent);
    lv_obj_set_pos(valLbl, x, y + 17);
    lv_obj_set_size(valLbl, w, 20);
    lv_label_set_text(valLbl, "--");
    lv_obj_set_style_text_color(valLbl, C(COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(valLbl, &montserrat_16_1bpp, LV_PART_MAIN);
    lv_label_set_long_mode(valLbl, LV_LABEL_LONG_CLIP);

    *outVal = valLbl;
    return keyLbl;
}

// ── makeSettingsRow: solid bg (fixes glyph blending) ─────────────────────────
static lv_obj_t* makeSettingsRow(lv_obj_t* parent, lv_coord_t y,
                                 lv_coord_t h, const char* label) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_pos(row, 0, y);
    lv_obj_set_size(row, 320, h);
    lv_obj_set_style_bg_color(row, C(COL_BG), LV_PART_MAIN);   // solid, not transparent
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


// ── makeListRow: full-width single-column row, large value font ───────────────
static lv_obj_t* makeListRow(lv_obj_t* parent, lv_coord_t y,
                              const char* key, lv_obj_t** outVal) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_pos(row, 0, y);
    lv_obj_set_size(row, 320, 44);
    lv_obj_set_style_bg_color(row, C(COL_BG), LV_PART_MAIN);   // solid bg → correct glyph blending
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
    lv_obj_set_style_bg_color(sw, C(COL_ACCENT),
        SEL(LV_PART_INDICATOR, LV_STATE_CHECKED));
    lv_obj_set_style_bg_color(sw, C(COL_TEXT), LV_PART_KNOB);
    if (cb) lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return sw;
}

// ─────────────────────────────────────────────────────────────────────────────
// Build UI
// ─────────────────────────────────────────────────────────────────────────────

// ─── convenience macro ────────────────────────────────────────────────────────
// Zero LVGL's default 12 px tab padding so lv_obj_set_pos(w,0,0) == screen edge
#define ZERO_TAB_PAD(t) \
    lv_obj_set_style_pad_all((t), 0, LV_PART_MAIN); \
    lv_obj_set_style_pad_row((t), 0, LV_PART_MAIN); \
    lv_obj_set_style_pad_column((t), 0, LV_PART_MAIN)

#define ZERO_TAB_PAD(t) \
    lv_obj_set_style_pad_all((t), 0, LV_PART_MAIN); \
    lv_obj_set_style_pad_row((t), 0, LV_PART_MAIN); \
    lv_obj_set_style_pad_column((t), 0, LV_PART_MAIN)

static void buildUi() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t* tv = lv_tabview_create(scr, LV_DIR_TOP, 28);
    lv_obj_set_size(tv, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_pos(tv, 0, 0);
    lv_obj_set_style_bg_color(tv, C(COL_BG), LV_PART_MAIN);

    lv_obj_t* bar = lv_tabview_get_tab_btns(tv);
    lv_obj_set_style_bg_color(bar, C(COL_CARD), LV_PART_MAIN);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(bar, C(COL_MUTED), LV_PART_ITEMS);
    lv_obj_set_style_text_font(bar, &montserrat_16_1bpp, LV_PART_ITEMS);
    lv_obj_set_style_text_color(bar, C(COL_ACCENT),
        SEL(LV_PART_ITEMS, LV_STATE_CHECKED));
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM,
        SEL(LV_PART_ITEMS, LV_STATE_CHECKED));
    lv_obj_set_style_border_color(bar, C(COL_ACCENT),
        SEL(LV_PART_ITEMS, LV_STATE_CHECKED));
    lv_obj_set_style_border_width(bar, 2,
        SEL(LV_PART_ITEMS, LV_STATE_CHECKED));

    // ══════════════════════════════════════════════════════════════════════
    // Tab 0 – MAIN  (no scroll, fixed 144 px)
    // ══════════════════════════════════════════════════════════════════════
    lv_obj_t* tabMain = lv_tabview_add_tab(tv, "MAIN");
    lv_obj_set_style_bg_color(tabMain, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabMain, LV_OPA_COVER, LV_PART_MAIN);
    ZERO_TAB_PAD(tabMain);
    lv_obj_clear_flag(tabMain, LV_OBJ_FLAG_SCROLLABLE);

    // SOC arc – 130×130, left side
    s_arcSoc = lv_arc_create(tabMain);
    lv_obj_set_size(s_arcSoc, 130, 130);
    lv_obj_set_pos(s_arcSoc, 4, 7);
    lv_arc_set_rotation(s_arcSoc, 135);
    lv_arc_set_bg_angles(s_arcSoc, 0, 270);
    lv_arc_set_range(s_arcSoc, 0, 100);
    lv_arc_set_value(s_arcSoc, 0);
    lv_obj_remove_style(s_arcSoc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(s_arcSoc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(s_arcSoc, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arcSoc, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_arcSoc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arcSoc, C(COL_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arcSoc, 10, LV_PART_INDICATOR);

    // SOC label – parented to tabMain (solid COL_BG) → correct glyph blending
    s_lblSocVal = lv_label_create(tabMain);
    lv_label_set_text(s_lblSocVal, "--%");
    lv_obj_set_style_text_font(s_lblSocVal, &montserrat_36_1bpp, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lblSocVal, C(COL_TEXT), LV_PART_MAIN);
    lv_obj_align_to(s_lblSocVal, s_arcSoc, LV_ALIGN_CENTER, 0, -40);

    // lv_obj_t* lblSocSub = lv_label_create(tabMain);
    // lv_label_set_text(lblSocSub, "State of Charge");
    // lv_obj_set_style_text_font(lblSocSub, &montserrat_12_1bpp, LV_PART_MAIN);
    // lv_obj_set_style_text_color(lblSocSub, C(COL_MUTED), LV_PART_MAIN);
    // lv_obj_align_to(lblSocSub, s_arcSoc, LV_ALIGN_CENTER, 0, 24);

    // Vertical divider
    static lv_point_t divPts[] = {{0, 4}, {0, 136}};
    lv_obj_t* div = lv_line_create(tabMain);
    lv_line_set_points(div, divPts, 2);
    lv_obj_set_pos(div, 142, 0);
    lv_obj_set_style_line_color(div, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_line_width(div, 1, LV_PART_MAIN);

    // Current
    s_lblPowerVal = lv_label_create(tabMain);
    lv_obj_set_pos(s_lblPowerVal, 150, 12);
    lv_obj_set_size(s_lblPowerVal, 166, 44);
    lv_label_set_text(s_lblPowerVal, "-- W");
    lv_label_set_long_mode(s_lblPowerVal, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_lblPowerVal, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lblPowerVal, &montserrat_36_1bpp, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lblPowerVal, C(COL_MUTED), LV_PART_MAIN);

    s_lblCurrentDir = lv_label_create(tabMain);
    lv_obj_set_pos(s_lblCurrentDir, 150, 58);
    lv_obj_set_size(s_lblCurrentDir, 166, 20);
    lv_label_set_text(s_lblCurrentDir, "No data");
    lv_obj_set_style_text_align(s_lblCurrentDir, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lblCurrentDir, &montserrat_16_1bpp, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lblCurrentDir, C(COL_MUTED), LV_PART_MAIN);

    // Separator + time remaining
    static lv_point_t sepPts[] = {{0, 0}, {160, 0}};
    lv_obj_t* sep = lv_line_create(tabMain);
    lv_line_set_points(sep, sepPts, 2);
    lv_obj_set_pos(sep, 150, 84);
    lv_obj_set_style_line_color(sep, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_line_width(sep, 1, LV_PART_MAIN);

    s_lblTimeLabel = lv_label_create(tabMain);
    lv_obj_set_pos(s_lblTimeLabel, 150, 90);
    lv_obj_set_size(s_lblTimeLabel, 166, 18);
    lv_label_set_text(s_lblTimeLabel, "Time remaining");
    lv_obj_set_style_text_align(s_lblTimeLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lblTimeLabel, &montserrat_14_1bpp, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lblTimeLabel, C(COL_MUTED), LV_PART_MAIN);

    s_lblTimeValue = lv_label_create(tabMain);
    lv_obj_set_pos(s_lblTimeValue, 150, 110);
    lv_obj_set_size(s_lblTimeValue, 166, 26);
    lv_label_set_text(s_lblTimeValue, "--");
    lv_obj_set_style_text_align(s_lblTimeValue, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lblTimeValue, &montserrat_20_1bpp, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lblTimeValue, C(COL_TEXT), LV_PART_MAIN);

    // BMS-only status indicator (bottom-right, no WiFi/MQTT)
    s_dotBms = lv_obj_create(tabMain);
    lv_obj_set_size(s_dotBms, 8, 8);
    lv_obj_set_pos(s_dotBms, 300, 134);
    lv_obj_set_style_radius(s_dotBms, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_dotBms, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_dotBms, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dotBms, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_dotBms, LV_OBJ_FLAG_SCROLLABLE);

    s_lblBmsStrip = lv_label_create(tabMain);
    lv_obj_set_pos(s_lblBmsStrip, 156, 131);
    lv_obj_set_size(s_lblBmsStrip, 135, 14);
    lv_label_set_text(s_lblBmsStrip, "BMS --");
    lv_obj_set_style_text_align(s_lblBmsStrip, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lblBmsStrip, &montserrat_12_1bpp, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lblBmsStrip, C(COL_MUTED), LV_PART_MAIN);

    // ══════════════════════════════════════════════════════════════════════
    // Tab 1 – PACK  (single-column scrollable list, 8 rows × 44 px = 352 px)
    // ══════════════════════════════════════════════════════════════════════
    lv_obj_t* tabPack = lv_tabview_add_tab(tv, "PACK");
    lv_obj_set_style_bg_color(tabPack, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabPack, LV_OPA_COVER, LV_PART_MAIN);
    ZERO_TAB_PAD(tabPack);
    lv_obj_add_flag(tabPack, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(tabPack, LV_DIR_VER);

    makeListRow(tabPack,   0, "Voltage",        &s_pkVoltage);
    makeListRow(tabPack,  44, "Power",          &s_pkPower);
    makeListRow(tabPack,  88, "Remaining",      &s_pkRemain);
    makeListRow(tabPack, 132, "Temperature",    &s_pkTemp);
    makeListRow(tabPack, 176, "Cycles",         &s_pkCycles);
    makeListRow(tabPack, 220, "Cell delta",     &s_pkCells);   // delta stays
    makeListRow(tabPack, 264, "Charge FET",     &s_pkChargeFet);
    makeListRow(tabPack, 308, "Discharge FET",  &s_pkDischargeFet);
    makeListRow(tabPack, 352, "SOC Limiter",    &s_pkLimiter);
    // Individual cell voltages
    makeListRow(tabPack, 396, "Cell 1",         &s_pkCell[0]);
    makeListRow(tabPack, 440, "Cell 2",         &s_pkCell[1]);
    makeListRow(tabPack, 484, "Cell 3",         &s_pkCell[2]);
    makeListRow(tabPack, 528, "Cell 4",         &s_pkCell[3]);

    // ══════════════════════════════════════════════════════════════════════
    // Tab 2 – INFO  (single-column scrollable list, 8 rows × 44 px = 352 px)
    // ══════════════════════════════════════════════════════════════════════
    lv_obj_t* tabInfo = lv_tabview_add_tab(tv, "INFO");
    lv_obj_set_style_bg_color(tabInfo, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabInfo, LV_OPA_COVER, LV_PART_MAIN);
    ZERO_TAB_PAD(tabInfo);
    lv_obj_add_flag(tabInfo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(tabInfo, LV_DIR_VER);

    makeListRow(tabInfo,   0, "IP Address",  &s_infoIp);
    makeListRow(tabInfo,  44, "WiFi",        &s_infoWifi);
    makeListRow(tabInfo,  88, "MQTT",        &s_infoMqtt);
    makeListRow(tabInfo, 132, "Uptime",      &s_infoUptime);
    makeListRow(tabInfo, 176, "BMS",         &s_infoBms);
    makeListRow(tabInfo, 220, "BMS Name",    &s_infoBmsName);
    makeListRow(tabInfo, 264, "Publishes",   &s_infoPub);
    makeListRow(tabInfo, 308, "SOC Limit",   &s_infoSocLim);
    makeListRow(tabInfo, 352, "WiFi Mode",   &s_infoWifiMode);
    makeListRow(tabInfo, 396, "AP SSID",     &s_infoApSsid);
    makeListRow(tabInfo, 440, "AP Password", &s_infoApPass);
    makeListRow(tabInfo, 484, "AP IP",       &s_infoApIp);

    // ══════════════════════════════════════════════════════════════════════
    // Tab 3 – SET   (single-column scrollable list, 4 rows × 44 px = 176 px)
    // ══════════════════════════════════════════════════════════════════════
    lv_obj_t* tabSet = lv_tabview_add_tab(tv, "SET");
    lv_obj_set_style_bg_color(tabSet, C(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabSet, LV_OPA_COVER, LV_PART_MAIN);
    ZERO_TAB_PAD(tabSet);
    lv_obj_add_flag(tabSet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(tabSet, LV_DIR_VER);

    // Row 0 – SOC limiter toggle + target dropdown
    lv_obj_t* row0 = makeSettingsRow(tabSet, 0, 54, "SOC Limiter");
    s_swLimiter = makeSwitch(row0, 168, 14, cbLimiterSwitch);
    s_ddLimiterSoc = lv_dropdown_create(row0);
    lv_obj_set_pos(s_ddLimiterSoc, 228, 10);
    lv_obj_set_size(s_ddLimiterSoc, 84, 40);
    lv_dropdown_set_options(s_ddLimiterSoc, "100%\n90%\n80%\n70%\n60%\n50%");
    lv_dropdown_set_selected(s_ddLimiterSoc, 2);
    lv_obj_set_style_bg_color(s_ddLimiterSoc, C(COL_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ddLimiterSoc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ddLimiterSoc, C(COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ddLimiterSoc, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_add_event_cb(s_ddLimiterSoc, cbLimiterSocDropdown, LV_EVENT_VALUE_CHANGED, nullptr);

    // Row 1 – Charge FET
    lv_obj_t* row1 = makeSettingsRow(tabSet, 54, 54, "Charge FET");
    s_swCharge = makeSwitch(row1, 260, 14, cbChargeSwitch);

    // Row 2 – Discharge FET
    lv_obj_t* row2 = makeSettingsRow(tabSet, 108, 54, "Discharge FET");
    s_swDischarge = makeSwitch(row2, 260, 14, cbDischargeSwitch);

    // Row 3 – Backlight slider
    lv_obj_t* row3 = makeSettingsRow(tabSet, 162, 54, "Backlight");
    s_sliderBl = lv_slider_create(row3);
    lv_obj_set_pos(s_sliderBl, 136, 17);
    lv_obj_set_size(s_sliderBl, 150, 20);
    lv_slider_set_range(s_sliderBl, 10, 100);
    lv_slider_set_value(s_sliderBl, 100, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_sliderBl, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_sliderBl, C(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_sliderBl, C(COL_TEXT), LV_PART_KNOB);
    lv_obj_set_style_border_color(s_sliderBl, C(COL_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_border_width(s_sliderBl, 2, LV_PART_KNOB);
    lv_obj_add_event_cb(s_sliderBl, cbBacklightSlider, LV_EVENT_VALUE_CHANGED, nullptr);

    // Row 4 – Display timeout
    lv_obj_t* row4 = makeSettingsRow(tabSet, 216, 54, "Screen off");
    s_ddTimeout = lv_dropdown_create(row4);
    lv_obj_set_pos(s_ddTimeout, 212, 10);
    lv_obj_set_size(s_ddTimeout, 100, 40);
    lv_dropdown_set_options(s_ddTimeout, "Never\n1 min\n10 min\n1 hour\n24 hours");
    lv_dropdown_set_selected(s_ddTimeout, 2);
    lv_obj_set_style_bg_color(s_ddTimeout, C(COL_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ddTimeout, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ddTimeout, C(COL_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ddTimeout, C(COL_BORDER), LV_PART_MAIN);
    lv_obj_add_event_cb(s_ddTimeout, cbTimeoutDropdown, LV_EVENT_VALUE_CHANGED, nullptr);

    // Row 5 – WiFi AP mode
    lv_obj_t* row5 = makeSettingsRow(tabSet, 270, 54, "WiFi AP mode");
    s_swAp = makeSwitch(row5, 260, 14, cbApSwitch);

    // Row 6 – BMS Simulation
    lv_obj_t* rowSim = makeSettingsRow(tabSet, 324, 54, "BMS Simulation");
    s_swSim = makeSwitch(rowSim, 260, 14, cbSimSwitch);

}


// ─────────────────────────────────────────────────────────────────────────────
// Update – MAIN tab
// ─────────────────────────────────────────────────────────────────────────────

static void updateMainTab() {
    bool valid = bmsIsDataValid();
    float cur  = bmsGetCurrent();
    uint8_t soc = valid ? bmsGetSOC() : 0;

    // Arc colour follows SOC
    lv_arc_set_value(s_arcSoc, valid ? soc : 0);
    lv_color_t arcCol = valid
        ? (soc >= 50 ? C(COL_GREEN) : soc >= 20 ? C(COL_YELLOW) : C(COL_RED))
        : C(COL_BORDER);
    lv_obj_set_style_arc_color(s_arcSoc, arcCol, LV_PART_INDICATOR);

    char buf[20];
    if (valid) snprintf(buf, sizeof(buf), "%u%%", soc);
    else       snprintf(buf, sizeof(buf), "--%%");
    lv_label_set_text(s_lblSocVal, buf);
    lv_obj_set_style_text_color(s_lblSocVal, valid ? arcCol : C(COL_MUTED), LV_PART_MAIN);
    lv_obj_align_to(s_lblSocVal, s_arcSoc, LV_ALIGN_CENTER, 0, -6);

    // Power
    if (valid && batEstIsValid()) {
        const BmsData& bms = bmsGetData();
        float P = bms.basic.current_A * bms.basic.totalVoltage_V;   // >0 discharge, <0 charge

        // Large power readout
        snprintf(buf, sizeof(buf), "%.0fW", fabsf(P));
        lv_label_set_text(s_lblPowerVal, buf);
        lv_obj_set_style_text_color(s_lblPowerVal,
            P >  5.0f ? C(COL_GREEN) :          // charging
            P < -5.0f ? C(COL_RED)   : C(COL_TEXT), LV_PART_MAIN);

        // Direction badge under it
        {
            char dirBuf[32];
            if (P > 5.0f) {
                if (configGetSocLimitEnabled())
                    snprintf(dirBuf, sizeof(dirBuf),
                        "Charging to %d%%", socLimiterGetThreshold());
                else
                    snprintf(dirBuf, sizeof(dirBuf), "Charging");
            } else if (P < -5.0f) {
                snprintf(dirBuf, sizeof(dirBuf), "Discharging");
            } else {
                snprintf(dirBuf, sizeof(dirBuf), "Idle");
            }
            lv_label_set_text(s_lblCurrentDir, dirBuf);
            lv_obj_set_style_text_color(s_lblCurrentDir,
                P > 5.0f ? C(COL_GREEN) :
                P < -5.0f ? C(COL_RED) : C(COL_MUTED), LV_PART_MAIN);
        }
    } else {
        lv_label_set_text(s_lblPowerVal, "-- W");
        lv_obj_set_style_text_color(s_lblPowerVal, C(COL_MUTED), LV_PART_MAIN);
        lv_label_set_text(s_lblCurrentDir, valid ? "Idle" : "No data");
        lv_obj_set_style_text_color(s_lblCurrentDir, C(COL_MUTED), LV_PART_MAIN);
    }

    // Time remaining / to full
    if (batEstIsValid()) {
        const BatEstimate& e = batEstGet();
        char tbuf[24];
        if (cur > 0.5f && e.chargeTimeValid) {
            lv_label_set_text(s_lblTimeLabel, "Time to full");
            batEstFormatTime(e.remainingChargeTime_s, tbuf, sizeof(tbuf));
            lv_label_set_text(s_lblTimeValue, tbuf);
        } else if (cur < -0.5f && e.dischargeTimeValid) {
            lv_label_set_text(s_lblTimeLabel, "Time to empty");
            batEstFormatTime(e.remainingDischargeTime_s, tbuf, sizeof(tbuf));
            lv_label_set_text(s_lblTimeValue, tbuf);
        } else {
            lv_label_set_text(s_lblTimeLabel, "Time remaining");
            lv_label_set_text(s_lblTimeValue, "Idle");
        }
    } else {
        lv_label_set_text(s_lblTimeLabel, "Time remaining");
        lv_label_set_text(s_lblTimeValue, "--");
    }

    // Status strip: BMS dot + text
    BmsState bs = bmsGetState();
    bool simulation = bmsIsSimulation();
    lv_color_t dotCol = (simulation == true)           ? C(COL_GREEN)
                      : (bs == BmsState::CONNECTED)    ? C(COL_GREEN)
                      : (bs == BmsState::DISCONNECTED)  ? C(COL_RED)
                      : C(COL_ORANGE);
    lv_obj_set_style_bg_color(s_dotBms, dotCol, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dotBms, LV_OPA_COVER, LV_PART_MAIN);

    const char* bmsText;
    if (simulation) {
        bmsText = "Simulation"; 
    } else {
        switch (bs) {
            case BmsState::CONNECTED:    bmsText = "OK";       break;
            case BmsState::CONNECTING:   bmsText = "BMS conn...";  break;
            case BmsState::SCANNING:     bmsText = "BMS scan...";  break;
            default:                     bmsText = "BMS offline"; break;
        }
    }
    lv_label_set_text(s_lblBmsStrip, bmsText);
    lv_obj_set_style_text_color(s_lblBmsStrip, dotCol, LV_PART_MAIN);

}

// ─────────────────────────────────────────────────────────────────────────────
// Update – PACK tab
// ─────────────────────────────────────────────────────────────────────────────

static void updatePackTab() {
    bool valid = bmsIsDataValid();
    char buf[32];

    // Voltage
    snprintf(buf, sizeof(buf), "%.2f V", bmsGetVoltage());
    lv_label_set_text(s_pkVoltage, valid ? buf : "--");
    lv_obj_set_style_text_color(s_pkVoltage, C(COL_ACCENT), LV_PART_MAIN);

    // Power
    if (batEstIsValid()) {
        snprintf(buf, sizeof(buf), "%.1f W", batEstGet().power_W);
        lv_label_set_text(s_pkPower, buf);
    } else {
        lv_label_set_text(s_pkPower, "--");
    }
    lv_obj_set_style_text_color(s_pkPower, C(COL_TEXT), LV_PART_MAIN);

    // Remaining capacity
    if (valid) {
        snprintf(buf, sizeof(buf), "%.2f Ah", bmsGetData().basic.remainCapacity_Ah);
        lv_label_set_text(s_pkRemain, buf);
    } else {
        lv_label_set_text(s_pkRemain, "--");
    }
    lv_obj_set_style_text_color(s_pkRemain, C(COL_TEXT), LV_PART_MAIN);

    // Temperature
    snprintf(buf, sizeof(buf), "%.1f C", bmsGetTemperature(0));
    lv_label_set_text(s_pkTemp, valid ? buf : "--");
    lv_obj_set_style_text_color(s_pkTemp, C(COL_TEXT), LV_PART_MAIN);

    // Cycles
    snprintf(buf, sizeof(buf), "%u", valid ? (unsigned)bmsGetData().basic.cycleCount : 0u);
    lv_label_set_text(s_pkCycles, valid ? buf : "--");
    lv_obj_set_style_text_color(s_pkCycles, C(COL_TEXT), LV_PART_MAIN);

    // Cell delta (min→max spread)
    if (valid && bmsGetData().cells.valid) {
        const BmsCellData& c = bmsGetData().cells;
        uint16_t minV = 65535, maxV = 0;
        for (uint8_t i = 0; i < c.cellCount && i < 4; i++) {
            if (c.cellVoltage_mV[i] < minV) minV = c.cellVoltage_mV[i];
            if (c.cellVoltage_mV[i] > maxV) maxV = c.cellVoltage_mV[i];
        }
        uint16_t delta = maxV - minV;
        snprintf(buf, sizeof(buf), "%u mV", delta);
        lv_label_set_text(s_pkCells, buf);
        lv_obj_set_style_text_color(s_pkCells,
            delta > 50 ? C(COL_RED) : delta > 20 ? C(COL_ORANGE) : C(COL_GREEN),
            LV_PART_MAIN);
    } else {
        lv_label_set_text(s_pkCells, "--");
        lv_obj_set_style_text_color(s_pkCells, C(COL_MUTED), LV_PART_MAIN);
    }

    // ── Charge FET ────────────────────────────────────────────────────────────
    if (valid) {
        bool chg = (bmsGetData().basic.fetStatus & 0x01) != 0;
        lv_label_set_text(s_pkChargeFet, chg ? "ON" : "OFF");
        lv_obj_set_style_text_color(s_pkChargeFet,
            chg ? C(COL_GREEN) : C(COL_RED), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_pkChargeFet, "--");
        lv_obj_set_style_text_color(s_pkChargeFet, C(COL_MUTED), LV_PART_MAIN);
    }

    // ── Discharge FET ─────────────────────────────────────────────────────────
    if (valid) {
        bool dis = (bmsGetData().basic.fetStatus & 0x02) != 0;
        lv_label_set_text(s_pkDischargeFet, dis ? "ON" : "OFF");
        lv_obj_set_style_text_color(s_pkDischargeFet,
            dis ? C(COL_GREEN) : C(COL_RED), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_pkDischargeFet, "--");
        lv_obj_set_style_text_color(s_pkDischargeFet, C(COL_MUTED), LV_PART_MAIN);
    }

    // ── SOC Limiter ───────────────────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "%s / %u%%",
        configGetSocLimitEnabled() ? "on" : "off",
        (unsigned)configGetMaxChargeSoc());
    lv_label_set_text(s_pkLimiter, buf);
    lv_obj_set_style_text_color(s_pkLimiter,
        configGetSocLimitEnabled() ? C(COL_ACCENT) : C(COL_MUTED), LV_PART_MAIN);

    // ── Individual cell voltages ───────────────────────────────────────────────
    if (valid && bmsGetData().cells.valid) {
        const BmsCellData& c = bmsGetData().cells;
        uint16_t minV = 65535, maxV = 0;
        for (uint8_t i = 0; i < c.cellCount && i < 4; i++) {
            if (c.cellVoltage_mV[i] < minV) minV = c.cellVoltage_mV[i];
            if (c.cellVoltage_mV[i] > maxV) maxV = c.cellVoltage_mV[i];
        }
        for (uint8_t i = 0; i < 4; i++) {
            if (i < c.cellCount) {
                uint16_t v = c.cellVoltage_mV[i];
                snprintf(buf, sizeof(buf), "%u mV", v);
                lv_label_set_text(s_pkCell[i], buf);
                // colour: lowest = red, highest = green, middle = white
                lv_color_t col = (v == minV && minV != maxV) ? C(COL_RED)
                               : (v == maxV && minV != maxV) ? C(COL_GREEN)
                               : C(COL_TEXT);
                lv_obj_set_style_text_color(s_pkCell[i], col, LV_PART_MAIN);
            } else {
                lv_label_set_text(s_pkCell[i], "n/a");
                lv_obj_set_style_text_color(s_pkCell[i], C(COL_MUTED), LV_PART_MAIN);
            }
        }
    } else {
        for (uint8_t i = 0; i < 4; i++) {
            lv_label_set_text(s_pkCell[i], "--");
            lv_obj_set_style_text_color(s_pkCell[i], C(COL_MUTED), LV_PART_MAIN);
        }
    }
}


// ─────────────────────────────────────────────────────────────────────────────
// Update – INFO tab
// ─────────────────────────────────────────────────────────────────────────────

static void updateInfoTab() {
    char buf[32];

    // IP
    lv_label_set_text(s_infoIp, wifiIsConnected() ? wifiGetIp().c_str() : "disconnected");
    lv_obj_set_style_text_color(s_infoIp,
        wifiIsConnected() ? C(COL_GREEN) : C(COL_RED), LV_PART_MAIN);

    // WiFi state + RSSI
    if (wifiIsConnected()) {
        snprintf(buf, sizeof(buf), "%ddBm", wifiGetRssi());
        lv_label_set_text(s_infoWifi, buf);
        lv_obj_set_style_text_color(s_infoWifi, C(COL_GREEN), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_infoWifi, "disconnected");
        lv_obj_set_style_text_color(s_infoWifi, C(COL_RED), LV_PART_MAIN);
    }

    // MQTT
    bool mq = mqttIsConnected();
    lv_label_set_text(s_infoMqtt, mq ? "connected" : "disconnected");
    lv_obj_set_style_text_color(s_infoMqtt, mq ? C(COL_GREEN) : C(COL_RED), LV_PART_MAIN);

    // Uptime
    uint32_t tot = millis() / 1000UL;
    uint32_t d   = tot / 86400UL; tot %= 86400UL;
    uint32_t h   = tot / 3600UL;  tot %= 3600UL;
    uint32_t m   = tot / 60UL;
    uint32_t s   = tot % 60UL;
    snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
            (unsigned long)d,(unsigned long)h,(unsigned long)m,(unsigned long)s);
    lv_label_set_text(s_infoUptime, buf);

    // BMS connection state
    bool bmsConn = (bmsGetState() == BmsState::CONNECTED);
    lv_label_set_text(s_infoBms, bmsConn ? "connected" : "searching...");
    lv_obj_set_style_text_color(s_infoBms,
        bmsConn ? C(COL_GREEN) : C(COL_ORANGE), LV_PART_MAIN);

    // BMS device name from config
    lv_label_set_text(s_infoBmsName, configGetBmsDeviceName());
    lv_obj_set_style_text_color(s_infoBmsName, C(COL_TEXT), LV_PART_MAIN);

    // MQTT publish count
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)mqttGetPublishCount());
    lv_label_set_text(s_infoPub, buf);
    lv_obj_set_style_text_color(s_infoPub, C(COL_TEXT), LV_PART_MAIN);

    // SOC limiter
    snprintf(buf, sizeof(buf), "%s / %u%%",
             configGetSocLimitEnabled() ? "on" : "off",
             (unsigned)configGetMaxChargeSoc());
    lv_label_set_text(s_infoSocLim, buf);
    lv_obj_set_style_text_color(s_infoSocLim,
        configGetSocLimitEnabled() ? C(COL_ACCENT) : C(COL_MUTED), LV_PART_MAIN);
    
    // WiFi mode
    if (wifiIsAp()) {
        lv_label_set_text(s_infoWifiMode, "AP");
        lv_obj_set_style_text_color(s_infoWifiMode, C(COL_ORANGE), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_infoWifiMode, "STA");
        lv_obj_set_style_text_color(s_infoWifiMode,
            wifiIsConnected() ? C(COL_GREEN) : C(COL_MUTED), LV_PART_MAIN);
    }

    // AP SSID
    lv_label_set_text(s_infoApSsid, configGetApSsid());
    lv_obj_set_style_text_color(s_infoApSsid,
        configGetApEnabled() ? C(COL_ACCENT) : C(COL_MUTED), LV_PART_MAIN);

    // AP passwd
    lv_label_set_text(s_infoApPass, configGetApPassword());
    lv_obj_set_style_text_color(s_infoApPass,
        configGetApEnabled() ? C(COL_TEXT) : C(COL_MUTED), LV_PART_MAIN);

    // AP IP
    lv_label_set_text(s_infoApIp,
        wifiIsAp() ? wifiGetApIp().c_str() : "--");
    lv_obj_set_style_text_color(s_infoApIp,
        wifiIsAp() ? C(COL_GREEN) : C(COL_MUTED), LV_PART_MAIN);
}

// ─────────────────────────────────────────────────────────────────────────────
// Update – SET tab
// ─────────────────────────────────────────────────────────────────────────────

static void updateSettingsTab() {
    // Serial.printf("[DISP] updateSettingsTab: limiter=%s  charge_fet=%s\n",
    //               configGetSocLimitEnabled() ? "on" : "off",
    //               bmsChargeFetOn() ? "on" : "off");

    socLimiterIsEnabled()
        ? lv_obj_add_state(s_swLimiter, LV_STATE_CHECKED)
        : lv_obj_clear_state(s_swLimiter, LV_STATE_CHECKED);

    s_suppressSwitchEvents = true;
    bmsChargeFetOn()
        ? lv_obj_add_state(s_swCharge, LV_STATE_CHECKED)
        : lv_obj_clear_state(s_swCharge, LV_STATE_CHECKED);
    bmsdischargeFetOn()
        ? lv_obj_add_state(s_swDischarge, LV_STATE_CHECKED)
        : lv_obj_clear_state(s_swDischarge, LV_STATE_CHECKED);
    s_suppressSwitchEvents = false;

    // Match dropdown to stored value
    static const uint8_t kVals[] = {100, 90, 80, 70, 60, 50};
    uint8_t stored = configGetMaxChargeSoc();
    uint16_t sel = 2; // default 90%
    for (uint16_t i = 0; i < 5; ++i) {
        if (kVals[i] == stored) { sel = i; break; }
    }
    lv_dropdown_set_selected(s_ddLimiterSoc, sel);

    configGetApEnabled()
        ? lv_obj_add_state(s_swAp, LV_STATE_CHECKED)
        : lv_obj_clear_state(s_swAp, LV_STATE_CHECKED);

    bmsIsSimulation()
        ? lv_obj_add_state(s_swSim, LV_STATE_CHECKED)
        : lv_obj_clear_state(s_swSim, LV_STATE_CHECKED);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API  (identical signatures to original)
// ─────────────────────────────────────────────────────────────────────────────

void displayInit() {
    lcdInit();
    lv_init();
    lv_disp_draw_buf_init(&s_drawBuf, s_buf1, s_buf2, LVGL_BUF_LEN);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res   = LCD_WIDTH;
    dispDrv.ver_res   = LCD_HEIGHT;
    dispDrv.flush_cb  = lvglFlush;
    dispDrv.full_refresh = 1;
    dispDrv.draw_buf  = &s_drawBuf;
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

    // restore saved backlight level
    uint8_t bl = configGetBacklight();
    lcdSetBacklight(bl);
    lv_slider_set_value(s_sliderBl, bl, LV_ANIM_OFF);

    static const uint16_t kTimeoutVals[] = {0, 60, 600, 3600, 65535};
    uint16_t stored = configGetDisplayTimeout();
    uint8_t  dtIdx  = 2;   // fallback: 10 min
    for (uint8_t i = 0; i < 5; i++) {
        if (kTimeoutVals[i] == stored) { dtIdx = i; break; }
    }
    lv_dropdown_set_selected(s_ddTimeout, dtIdx);

    Serial.printf("[DISP] Free heap after buildUi:  %lu bytes\n",
                  (unsigned long)ESP.getFreeHeap());

    s_lastRefreshMs = millis();
    Serial.println("[DISP] UI init OK – dark theme");
}


void displayTask() {
    lv_timer_handler();

    // deferred backlight EEPROM save — 10 s after last slider movement
    if (s_blSavePendingMs != 0 &&
        (millis() - s_blSavePendingMs) >= 10000UL) {
        s_blSavePendingMs = 0;
        configSave();
        Serial.println("[DISP] Backlight saved to EEPROM");
    }

    uint32_t now = millis();
    if (s_forceRefresh || (now - s_lastRefreshMs >= DISPLAY_REFRESH_MS)) {
        s_lastRefreshMs = now;
        s_forceRefresh  = false;
        updateMainTab();
        updatePackTab();
        updateInfoTab();
        updateSettingsTab();
    }
}


void displayRefresh() { s_forceRefresh = true; }

void displayApplyConfig() {
    if (!s_sliderBl || !s_ddTimeout || !s_swLimiter ||
        !s_ddLimiterSoc || !s_swAp) return;   // UI not built yet

    // Backlight slider
    lv_slider_set_value(s_sliderBl, configGetBacklight(), LV_ANIM_OFF);
    lcdSetBacklight(configGetBacklight());

    // Display timeout dropdown
    static const uint16_t kTimeoutVals[] = {0, 60, 600, 3600, 65535};
    uint16_t storedTimeout = configGetDisplayTimeout();
    uint8_t dtIdx = 2;
    for (uint8_t i = 0; i < 5; i++) {
        if (kTimeoutVals[i] == storedTimeout) { dtIdx = i; break; }
    }
    lv_dropdown_set_selected(s_ddTimeout, dtIdx);

    // SOC limiter toggle + dropdown
    if (configGetSocLimitEnabled())
        lv_obj_add_state(s_swLimiter, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(s_swLimiter, LV_STATE_CHECKED);

    static const uint8_t kSocVals[] = {100, 90, 80, 70, 60, 50};
    uint8_t storedSoc = configGetMaxChargeSoc();
    uint8_t socIdx = 2;
    for (uint8_t i = 0; i < 5; i++) {
        if (kSocVals[i] == storedSoc) { socIdx = i; break; }
    }
    lv_dropdown_set_selected(s_ddLimiterSoc, socIdx);

    // AP mode toggle
    if (configGetApEnabled())
        lv_obj_add_state(s_swAp, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(s_swAp, LV_STATE_CHECKED);

    s_forceRefresh = true;
}
