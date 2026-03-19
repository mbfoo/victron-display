#pragma once
#include <lvgl.h>

void touchInit();
void lvglTouchRead(lv_indev_drv_t* drv, lv_indev_data_t* data);
