#pragma once
#include <Arduino.h>

void displayTimeoutInit();
void displayTimeoutTask();

// Call on touch press. Returns true if display was OFF (wake-only touch — suppress to LVGL).
bool displayTimeoutOnTouch();

bool displayTimeoutIsOn();
