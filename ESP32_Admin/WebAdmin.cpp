#include "WebAdmin.h"
#include <WebServer.h>

extern WebServer server;
extern bool webStarted;

void handleRoot();
void handleApiConfig();
void handleSaveDisplay();
void handleSetPage();
void handleScanWiFi();
void handleSaveWiFi();
void handleWiFiMode();
void handleSaveWiFiDefaults();
void handleSetClock();
void handleNotFound();

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleApiConfig);
  server.on("/api/scan", HTTP_GET, handleScanWiFi);
  server.on("/api/save", HTTP_POST, handleSaveDisplay);
  server.on("/api/clock", HTTP_POST, handleSetClock);
  server.on("/api/page", HTTP_GET, handleSetPage);
  server.on("/api/page", HTTP_POST, handleSetPage);
  server.on("/api/wifi", HTTP_POST, handleSaveWiFi);
  server.on("/api/wifi-mode", HTTP_POST, handleWiFiMode);
  server.on("/api/wifi-defaults", HTTP_POST, handleSaveWiFiDefaults);
  server.onNotFound(handleNotFound);
  server.begin();
  webStarted = true;
  Serial.println(F("Web admin started on port 80."));
}
