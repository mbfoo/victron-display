#include "web_server.h"
#include "web_api.h"
#include <WebServer.h>
#include <SD.h>

static WebServer s_server(80);

// ─── Static file handler ─────────────────────────────────────────────────────

static void handleStaticFile() {
    String path = s_server.uri();

    if (path.startsWith("/api/")) {
        s_server.send(404, "text/plain", "API endpoint not found");
        return;
    }

    if (path.endsWith("/")) path += "index.html";
    String sdPath = "/data" + path;

    File f = SD.open(sdPath);
    if (!f || f.isDirectory()) {
        s_server.send(404, "text/plain", "Not found: " + sdPath);
        return;
    }

    String mime = "text/plain";
    if (sdPath.endsWith(".html"))      mime = "text/html";
    else if (sdPath.endsWith(".css"))  mime = "text/css";
    else if (sdPath.endsWith(".js"))   mime = "application/javascript";
    else if (sdPath.endsWith(".json")) mime = "application/json";
    else if (sdPath.endsWith(".ico"))  mime = "image/x-icon";
    else if (sdPath.endsWith(".png"))  mime = "image/png";

    s_server.streamFile(f, mime);
    f.close();
}

// ─── Init / Task ─────────────────────────────────────────────────────────────

void webServerInit() {
    s_server.on("/history", HTTP_GET, []() {
        s_server.sendHeader("Location", "/history.html");
        s_server.send(302, "text/plain", "");
    });
    webApiRegisterRoutes(s_server);          // all /api/ routes first
    s_server.onNotFound(handleStaticFile);   // static fallback last
    s_server.begin();
    Serial.println("[WEB] Server started on port 80");
}

void webServerTask() {
    s_server.handleClient();
}
