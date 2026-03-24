#pragma once
#define LV_CONF_SKIP 0

#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   1

#define LV_HOR_RES_MAX     320
#define LV_VER_RES_MAX     172

// Tick – provided by lv_tick_inc() in displayTask()
#define LV_TICK_CUSTOM     0

// Memory
// #define LV_MEM_CUSTOM      0
// #define LV_MEM_SIZE        (24U * 1024U)

#define LV_MEM_CUSTOM      1
#define LV_MEM_CUSTOM_INCLUDE  <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC    malloc
#define LV_MEM_CUSTOM_FREE     free
#define LV_MEM_CUSTOM_REALLOC  realloc

// Fonts – enable every size used in the YAML
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_22   1
#define LV_FONT_MONTSERRAT_36   1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT        &lv_font_montserrat_16

// Widgets needed
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_TEXTAREA   1
#define LV_USE_SWITCH     1
#define LV_USE_DROPDOWN   1
#define LV_USE_SLIDER     1
#define LV_USE_TABVIEW    1
#define LV_USE_BTN        1

#define LV_USE_ANIMATION  1
#define LV_USE_THEME_DEFAULT 1

// Anti-aliasing – should already be 1
#define LV_DRAW_COMPLEX   1

// Subpixel rendering – disable this if you see colour fringes on small text
#define LV_FONT_SUBPX_EN  1   // set to 0 to use plain alpha AA instead

// Only relevant if SUBPX_EN=1 – must match panel physical stripe order
#define LV_FONT_SUBPX_BGR 0   // 0 = RGB order, 1 = BGR order