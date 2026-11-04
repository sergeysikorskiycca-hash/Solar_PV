#include "WebAdmin.h"
#include <WebServer.h>

extern WebServer server;
extern bool webStarted;

void handleRoot();
void handleApiConfig();
void handleSaveDisplay();
void handleSaveUiTexts();
void handleSetPage();
void handleScanWiFi();
void handleWiFiScanStatus();
void handleSaveWiFi();
void handleWiFiMode();
void handleSaveWiFiDefaults();
void handleSetClock();
void handleNotFound();

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleApiConfig);
  server.on("/api/scan", HTTP_GET, handleScanWiFi);
  server.on("/api/scan-status", HTTP_GET, handleWiFiScanStatus);
  server.on("/api/save", HTTP_POST, handleSaveDisplay);
  server.on("/api/ui-texts", HTTP_POST, handleSaveUiTexts);
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

#include <ArduinoJson.h>
#include "AppConfig.h"
#include "Display.h"
static void readUiText(JsonObjectConst doc,const char* key,String& target,const char* fallback) {
 if(doc.containsKey(key))target=sanitizeUiText(String(doc[key] | ""),fallback);
}
void handleSaveUiTexts() {
 DynamicJsonDocument doc(2048);
 if(deserializeJson(doc,server.arg("plain"))){server.send(400,"application/json; charset=utf-8","{\\"ok\\":false,\\"message\\":\\"Некорректные тексты интерфейса.\\"}");return;}
 UiTexts oldTexts=cfg.uiTexts;
 JsonObjectConst values=doc.as<JsonObjectConst>();
 readUiText(values,"stringPrefix",cfg.uiTexts.stringPrefix,"Стринг");
 readUiText(values,"currentLabel",cfg.uiTexts.currentLabel,"Ток");
 readUiText(values,"voltageLabel",cfg.uiTexts.voltageLabel,"Напр.");
 readUiText(values,"routerLabel",cfg.uiTexts.routerLabel,"Роутер");
 readUiText(values,"apLabel",cfg.uiTexts.apLabel,"Точка");
 readUiText(values,"noRouterLabel",cfg.uiTexts.noRouterLabel,"Роутер не подключён");
 readUiText(values,"networkLabel",cfg.uiTexts.networkLabel,"Сеть");
 readUiText(values,"signalLabel",cfg.uiTexts.signalLabel,"Сигнал");
 readUiText(values,"apSetupLabel",cfg.uiTexts.apSetupLabel,"Точка настройки:");
 readUiText(values,"selectHint",cfg.uiTexts.selectHint,"ВВЕРХ/ВНИЗ: выбор");
 readUiText(values,"changeHint",cfg.uiTexts.changeHint,"OK: изменить (5 сек.)");
 readUiText(values,"applyLabel",cfg.uiTexts.applyLabel,"Применение через");
 if(!saveConfig()){cfg.uiTexts=oldTexts;server.send(500,"application/json; charset=utf-8","{\\"ok\\":false,\\"message\\":\\"Не удалось сохранить подписи в LittleFS.\\"}");return;}
 renderTFT();server.send(200,"application/json; charset=utf-8","{\\"ok\\":true,\\"message\\":\\"Подписи интерфейса сохранены.\\"}");
}
