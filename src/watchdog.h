#pragma once
#include <stdint.h>

#ifndef WDT_TIMEOUT_MS
#define WDT_TIMEOUT_MS 30000
#endif

void     watchdogInit();
void     watchdogTask();
void     watchdogPrint();
uint32_t watchdogGetUptimeMs();
