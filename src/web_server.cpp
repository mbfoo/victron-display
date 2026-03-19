#include "web_server.h"
#include "web_api.h"
#include <WebServer.h>
#include <Arduino.h>

WebServer g_server(80);

void webServerInit() {
    webApiRegisterRoutes(g_server);
    g_server.begin();
    Serial.println("[WEB] Server started on port 80");
}

void webServerTask() {
    g_server.handleClient();
}
