#pragma once
#include <Arduino.h>

/** Call once from setup() — starts the server on port 80. */
void webServerInit();

/** Call every loop() — handles incoming client requests (non-blocking). */
void webServerTask();

/** True if at least one HTTP request has been served. */
bool webServerIsActive();
