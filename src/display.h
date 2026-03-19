#pragma once
#include <Arduino.h>
#include <lvgl.h>

LV_FONT_DECLARE(montserrat_12_1bpp);
LV_FONT_DECLARE(montserrat_14_1bpp);
LV_FONT_DECLARE(montserrat_16_1bpp);
LV_FONT_DECLARE(montserrat_20_1bpp);
LV_FONT_DECLARE(montserrat_36_1bpp);

void displayInit();   // call once from setup()
void displayTask();   // call every loop() – non-blocking
void displayRefresh();
void displayApplyConfig();