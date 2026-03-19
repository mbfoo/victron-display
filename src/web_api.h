#pragma once

#include <WebServer.h>

// Call once from webServerInit() to register all /api/ routes
void webApiRegisterRoutes(WebServer& server);
