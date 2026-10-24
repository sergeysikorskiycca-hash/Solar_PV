/*
  Solar_PV — ESP32 Admin MVP
  Target: ESP32 Dev Module, Arduino-ESP32 core 3.x
  TFT: ST7789 240x320, Adafruit_GFX + Adafruit_ST7789
  Current stage: TFT, Wi-Fi setup, browser admin, LittleFS settings, NEC IR remote,
  and checked I2C telemetry from the ESP8266 gateway (DS3231 + ESP-NOW sensor samples).
*/

#include <WiFi.h>
#include <Wire.h>
#include <string.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "DisplayConfig.h"
#include "AppConfig.h"
#include "ConfigStore.h"
#include "Display.h"

// Explicit declaration keeps incremental widget refresh visible to Arduino's sketch preprocessor.
void drawWidget(const WidgetConfig& widget);
#include "WifiManager.h"
#include "WebAdmin.h"
#include "WebPage.h"
#include "IR_Remote.h"

const char* AP_SSID = "SolarPV-Setup";
const char* AP_PASS = "SolarPV8266";
Adafruit_ST7789 tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
WebServer server(80);

bool apActive = false;
extern bool routerModeEnabled;
extern bool setupApEnabled;
void applyWiFiMode();

bool wifiControlPendingRouter = false;
bool wifiControlPendingAp = true;
bool wifiControlApplyPending = false;
uint8_t wifiControlSelectedRow = 0;
uint32_t wifiControlApplyAt = 0;
static bool hasWifiControlOnCurrentPage() {
  if (cfg.tftPage < 1 || cfg.tftPage > cfg.pageCount) return false;
  PageConfig& page = cfg.pages[cfg.tftPage - 1];
  for (uint8_t i = 0; i < page.widgetCount; ++i)
    if (page.widgets[i].enabled && page.widgets[i].type == "wifiControl") return true;
  return false;
}
static void refreshWifiControlWidget() {
  if (cfg.tftPage < 1 || cfg.tftPage > cfg.pageCount) return;
  PageConfig& page = cfg.pages[cfg.tftPage - 1];
  for (uint8_t i = 0; i < page.widgetCount; ++i)
    if (page.widgets[i].enabled && page.widgets[i].type == "wifiControl") drawWidget(page.widgets[i]);
}
static void handleInlineWifiControl(uint32_t code) {
  if (code == IR_CODE_UP || code == IR_CODE_DOWN) {
    wifiControlSelectedRow = wifiControlSelectedRow ? 0 : 1;
    refreshWifiControlWidget();
  } else if (code == IR_CODE_OK) {
    if (!wifiControlApplyPending) {
      wifiControlPendingRouter = routerModeEnabled;
      wifiControlPendingAp = setupApEnabled;
    }
    if (wifiControlSelectedRow == 0) wifiControlPendingRouter = !wifiControlPendingRouter;
    else wifiControlPendingAp = !wifiControlPendingAp;
    wifiControlApplyPending = true;
    wifiControlApplyAt = millis() + 5000UL;
    refreshWifiControlWidget();
  }
}

static volatile uint8_t clockCommandSequence = 0;
static volatile uint8_t clockCommandType = 0;
static volatile uint16_t clockCommandYear = 2026;
static volatile uint8_t clockCommandMonth = 1, clockCommandDay = 1, clockCommandHour = 0, clockCommandMinute = 0, clockCommandSecond = 0;
static volatile bool clockCommandPending = false;

// ESP8266 gateway telemetry arrives as a checked I2C frame.
bool gatewayOnline = false;
bool gatewayTimeValid = false;
uint32_t gatewayLastPacketMs = 0;
uint16_t gatewayYear = 0;
uint8_t gatewayMonth = 0, gatewayDay = 0, gatewayHour = 0, gatewayMinute = 0, gatewaySecond = 0;
uint8_t gatewaySensorId = 0;
float gatewayVoltage = 0.0f, gatewayCurrent = 0.0f, gatewayPower = 0.0f, gatewayShuntVoltage = 0.0f;
uint16_t gatewaySensorPacketNumber = 0;
bool gatewaySensorValid = false;
static uint32_t lastIndicatorDraw = 0;
static uint32_t lastGatewayLogMs = 0;
bool webStarted = false;
unsigned long disconnectedSince = 0;
unsigned long restartAt = 0;
unsigned long lastStatusDraw = 0;
String lastStatusText = "";








String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n' || c == '\r') { out += ' '; }
    else if ((uint8_t)c >= 0x20) { out += c; }
  }
  return out;
}























String jsonEscapeText(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if ((uint8_t)c >= 0x20) out += c;
  }
  return out;
}

String makeConfigJson() {
  String j = "{";
  j += "\"title\":\"" + jsonEscape(cfg.title) + "\",";
  j += "\"powerLabel\":\"" + jsonEscape(cfg.powerLabel) + "\",";
  j += "\"voltageLabel\":\"" + jsonEscape(cfg.voltageLabel) + "\",";
  j += "\"currentLabel\":\"" + jsonEscape(cfg.currentLabel) + "\",";
  j += "\"batteryLabel\":\"" + jsonEscape(cfg.batteryLabel) + "\",";
  j += "\"fontScale\":" + String(cfg.fontScale) + ",";
  j += "\"showPower\":" + String(cfg.showPower ? "true" : "false") + ",";
  j += "\"showVoltage\":" + String(cfg.showVoltage ? "true" : "false") + ",";
  j += "\"showCurrent\":" + String(cfg.showCurrent ? "true" : "false") + ",";
  j += "\"showBattery\":" + String(cfg.showBattery ? "true" : "false") + ",";
  j += "\"powerX\":" + String(cfg.powerX) + ",\"powerY\":" + String(cfg.powerY) + ",";
  j += "\"voltageX\":" + String(cfg.voltageX) + ",\"voltageY\":" + String(cfg.voltageY) + ",";
  j += "\"currentX\":" + String(cfg.currentX) + ",\"currentY\":" + String(cfg.currentY) + ",";
  j += "\"batteryX\":" + String(cfg.batteryX) + ",\"batteryY\":" + String(cfg.batteryY) + ",";
  j += "\"powerW\":" + String(cfg.powerW) + ",\"powerH\":" + String(cfg.powerH) + ",";
  j += "\"voltageW\":" + String(cfg.voltageW) + ",\"voltageH\":" + String(cfg.voltageH) + ",";
  j += "\"currentW\":" + String(cfg.currentW) + ",\"currentH\":" + String(cfg.currentH) + ",";
  j += "\"batteryW\":" + String(cfg.batteryW) + ",\"batteryH\":" + String(cfg.batteryH) + ",";
  j += "\"clock\":{\"valid\":" + String(gatewayTimeValid ? "true" : "false") + ",\"date\":\"";
  if (gatewayTimeValid) { char d[11]; snprintf(d, sizeof(d), "%04u-%02u-%02u", (unsigned)gatewayYear, (unsigned)gatewayMonth, (unsigned)gatewayDay); j += d; }
  j += "\",\"time\":\"";
  if (gatewayTimeValid) { char t[9]; snprintf(t, sizeof(t), "%02u:%02u:%02u", (unsigned)gatewayHour, (unsigned)gatewayMinute, (unsigned)gatewaySecond); j += t; }
  j += "\"},\"ssid\":\"" + jsonEscape(cfg.ssid) + "\",";
  j += "\"savedNetworks\":[";
  bool firstSaved = true;
  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; i++) {
    if (!savedNetworks[i].ssid.length()) continue;
    if (!firstSaved) j += ",";
    firstSaved = false;
    j += "{\"ssid\":\"" + jsonEscape(savedNetworks[i].ssid) + "\"}";
  }
  j += "],";
  String net;
  String label;
  if (WiFi.status() == WL_CONNECTED) {
    net = "Подключено к роутеру: " + WiFi.localIP().toString() +
          " · уровень сигнала: " + String(WiFi.RSSI()) + " dBm";
    label = "Wi-Fi: " + String(WiFi.RSSI()) + " dBm";
  } else if (apActive) {
    net = "Точка настройки активна: SolarPV-Setup · адрес 192.168.4.1";
    label = "Режим настройки";
  } else {
    net = "Wi-Fi не подключён";
    label = "Wi-Fi: нет связи";
  }
  j += "\"networkStatus\":\"" + jsonEscape(net) + "\",";
  j += "\"networkLabel\":\"" + jsonEscape(label) + "\",";
  j += "\"tftPage\":" + String(cfg.tftPage) + ",";
  j += "\"pageCount\":" + String(cfg.pageCount) + ",\"pages\":[";
  for (uint8_t p = 0; p < cfg.pageCount; p++) {
    if (p) j += ",";
    const PageConfig& page = cfg.pages[p];
    j += "{\"title\":\"" + jsonEscape(page.title) + "\",\"showTitle\":" + String(page.showTitle ? "true" : "false") + ",\"widgetCount\":" + String(page.widgetCount) + ",\"widgets\":[";
    for (uint8_t i = 0; i < page.widgetCount; i++) {
      if (i) j += ",";
      const WidgetConfig& w = page.widgets[i];
      j += "{\"type\":\"" + jsonEscape(w.type) + "\",\"label\":\"" + jsonEscape(w.label) + "\",\"text\":\"" + jsonEscapeText(w.text) + "\",\"showHeading\":" + String(w.showHeading ? "true" : "false") + ",\"showBorder\":" + String(w.showBorder ? "true" : "false") + ",";
      j += "\"headingSize\":" + String(w.headingSize) + ",\"contentSize\":" + String(w.contentSize) + ",\"stringCount\":" + String(w.stringCount ? w.stringCount : 2) + ",\"locked\":" + String(w.locked ? "true" : "false") + ",\"headingAlign\":" + String(w.headingAlign) + ",\"contentAlign\":" + String(w.contentAlign) + ",\"headingColor\":" + String(w.headingColor) + ",\"contentColor\":" + String(w.contentColor) + ",\"backgroundColor\":" + String(w.backgroundColor) + ",";
      j += "\"x\":" + String(w.x) + ",\"y\":" + String(w.y) + ",\"w\":" + String(w.w) + ",\"h\":" + String(w.h) + ",";
      j += "\"enabled\":" + String(w.enabled ? "true" : "false") + "}";
    }
    j += "]}";
  }
  j += "]}";
  j.remove(j.length() - 1); j += ",\"wifiMode\":{\"router\":" + String(routerModeEnabled ? "true" : "false") + ",\"ap\":" + String(setupApEnabled ? "true" : "false") + ",\"apActive\":" + String(apActive ? "true" : "false") + ",\"defaultRouter\":" + String(cfg.defaultRouterMode ? "true" : "false") + ",\"defaultAp\":" + String(cfg.defaultSetupAp ? "true" : "false") + "}}";
  return j;
}

void stopAccessPointIfConnected() {
  // Intentionally keep the setup AP available while connected to the router.
  // No repeated Serial logging from loop().
}


void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleApiConfig() {
  server.send(200, "application/json; charset=utf-8", makeConfigJson());
}

void handleSaveDisplay() {
  DynamicJsonDocument doc(32768);
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Некорректный JSON макета.\"}");
    return;
  }
  if (doc.containsKey("title")) cfg.title = sanitizeTitle(String(doc["title"] | "SOLAR PV"));
  if (doc.containsKey("fontScale")) cfg.fontScale = (doc["fontScale"] | 1) == 2 ? 2 : 1;
  JsonArray sourcePages = doc["pages"].as<JsonArray>();
  uint8_t requestedCount = clampInt(doc["pageCount"] | sourcePages.size(), 1, MAX_PAGES);
  if (sourcePages.isNull() || sourcePages.size() < requestedCount) {
    server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"В конфигурации не хватает страниц.\"}");
    return;
  }
  cfg.pageCount = requestedCount;
  for (uint8_t p = 0; p < cfg.pageCount; p++) {
    JsonObject sourcePage = sourcePages[p].as<JsonObject>();
    String pageTitle = String(sourcePage["title"] | "");
    if (!pageTitle.length()) pageTitle = "Страница " + String(p + 1);
    cfg.pages[p].title = sanitizeTitle(pageTitle);
    cfg.pages[p].showTitle = sourcePage["showTitle"] | true;
    JsonArray sourceWidgets = sourcePage["widgets"].as<JsonArray>();
    cfg.pages[p].widgetCount = clampInt(sourceWidgets.size(), 0, MAX_WIDGETS_PER_PAGE);
    for (uint8_t i = 0; i < cfg.pages[p].widgetCount; i++) {
      JsonObject source = sourceWidgets[i].as<JsonObject>();
      WidgetConfig& w = cfg.pages[p].widgets[i];
      String type = String(source["type"] | "power");
      if (isKnownWidgetType(type)) w.type = type;
      w.label = sanitizeTitle(String(source["label"] | "Блок"));
      w.text = String(source["text"] | "");
      if (w.text.length() > 240) w.text = w.text.substring(0, 240);
      w.headingSize = clampInt(source["headingSize"] | 1, 0, 5); w.contentSize = clampInt(source["contentSize"] | 2, 0, 5);
      w.stringCount = clampInt(source["stringCount"] | 2, 1, 6); w.locked = source["locked"] | false; w.showHeading = source["showHeading"] | true; w.showBorder = source["showBorder"] | true;
      w.headingAlign = clampInt(source["headingAlign"] | 0, 0, 2); w.contentAlign = clampInt(source["contentAlign"] | 0, 0, 2);
      w.headingColor = clampInt(source["headingColor"] | 0, 0, 5); w.contentColor = clampInt(source["contentColor"] | 3, 0, 5); w.backgroundColor = clampInt(source["backgroundColor"] | 0, 0, 6);
      w.w = clampInt(source["w"] | 106, 40, 240);
      w.h = clampInt(source["h"] | 58, 36, 250);
      w.x = clampInt(source["x"] | 8, 0, 240 - w.w);
      w.y = clampInt(source["y"] | 55, 40, 290 - w.h);
      w.enabled = source["enabled"] | true;
    }
  }
  for (uint8_t i = 0; i < cfg.pages[0].widgetCount; i++) {
    WidgetConfig& w = cfg.pages[0].widgets[i];
    if (w.type == "power") { cfg.powerLabel=w.label; cfg.showPower=w.enabled; cfg.powerX=w.x; cfg.powerY=w.y; cfg.powerW=w.w; cfg.powerH=w.h; }
    else if (w.type == "voltage") { cfg.voltageLabel=w.label; cfg.showVoltage=w.enabled; cfg.voltageX=w.x; cfg.voltageY=w.y; cfg.voltageW=w.w; cfg.voltageH=w.h; }
    else if (w.type == "current") { cfg.currentLabel=w.label; cfg.showCurrent=w.enabled; cfg.currentX=w.x; cfg.currentY=w.y; cfg.currentW=w.w; cfg.currentH=w.h; }
    else if (w.type == "battery") { cfg.batteryLabel=w.label; cfg.showBattery=w.enabled; cfg.batteryX=w.x; cfg.batteryY=w.y; cfg.batteryW=w.w; cfg.batteryH=w.h; }
  }
  cfg.tftPage = clampInt(cfg.tftPage, 1, cfg.pageCount);
  bool ok = saveConfig(); renderTFT();
  String message = ok ? "Макет страниц сохранён и применён." : "Макет применён, но не сохранён: файловая система недоступна.";
  server.send(ok ? 200 : 500, "application/json; charset=utf-8", String("{\"ok\":") + (ok ? "true" : "false") + ",\"message\":\"" + message + "\"}");
}
void handleSetPage() {
  if (server.arg("action") == "create") {
    if (cfg.pageCount >= MAX_PAGES) { server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Достигнут лимит страниц (4).\"}"); return; }
    uint8_t index = cfg.pageCount++;
    cfg.pages[index].title = "Страница " + String(index + 1); cfg.pages[index].widgetCount = 0;
    if (!saveConfig()) { cfg.pageCount--; server.send(500, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Не удалось сохранить страницу.\"}"); return; }
    server.send(200, "application/json; charset=utf-8", String("{\"ok\":true,\"pageCount\":") + String(cfg.pageCount) + ",\"page\":" + String(index + 1) + "}"); return;
  }
  int page = server.arg("page").toInt();
  if (page < 1 || page > cfg.pageCount) { server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Такой страницы нет.\"}"); return; }
  cfg.tftPage = page;
  if (!saveConfig()) { server.send(500, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Не удалось сохранить страницу в LittleFS.\"}"); return; }
  server.send(200, "application/json; charset=utf-8", String("{\"ok\":true,\"message\":\"На дисплее страница ") + String(page) + "\"}");
  delay(20); renderTFT();
}
void handleScanWiFi() {
  // Scanning must not silently override the temporary router/AP switches.
  if (!routerModeEnabled && !setupApEnabled) {
    server.send(409, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Сначала включите роутер или точку доступа.\"}");
    return;
  }
  Serial.println(F("Wi-Fi scan requested from admin page."));
  int count = WiFi.scanNetworks(false, true);
  if (count < 0) {
    server.send(500, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Не удалось выполнить сканирование Wi-Fi.\"}");
    return;
  }

  String json = "{\"ok\":true,\"networks\":[";
  bool first = true;
  for (int i = 0; i < count; i++) {
    String networkName = WiFi.SSID(i);
    if (networkName.length() == 0) continue; // Ignore hidden SSIDs.
    if (!first) json += ",";
    first = false;
    int rssi = WiFi.RSSI(i);
    bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    json += "{\"ssid\":\"" + jsonEscape(networkName) + "\",";
    json += "\"rssi\":" + String(rssi) + ",";
    json += "\"secure\":" + String(secure ? "true" : "false") + "}";
  }
  json += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json; charset=utf-8", json);
}

void handleSaveWiFiDefaults() {\n  DynamicJsonDocument doc(256);\n  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json; charset=utf-8", "{\\\"ok\\\":false,\\\"message\\\":\\\"Некорректные настройки.\\\"}"); return; }\n  cfg.defaultRouterMode = doc["router"] | cfg.defaultRouterMode;\n  cfg.defaultSetupAp = doc["ap"] | cfg.defaultSetupAp;\n  if (!saveConfig()) { server.send(500, "application/json; charset=utf-8", "{\\\"ok\\\":false,\\\"message\\\":\\\"Не удалось сохранить настройки.\\\"}"); return; }\n  server.send(200, "application/json; charset=utf-8", "{\\\"ok\\\":true,\\\"message\\\":\\\"Настройки запуска сохранены. Текущий режим не изменён; новые значения применятся после перезагрузки.\\\"}");\n}\n\nvoid handleWiFiMode() {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Некорректные настройки Wi-Fi.\"}"); return; }
  routerModeEnabled = doc["router"] | routerModeEnabled;
  setupApEnabled = doc["ap"] | setupApEnabled;
  applyWiFiMode();
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"message\":\"Временный режим применён. После перезагрузки включится только точка SolarPV-Setup.\"}");
}

void handleSaveWiFi() {
  String newSsid = server.arg("ssid");
  String newPassword = server.arg("password");
  newSsid.trim();
  if (newSsid.length() == 0 || newSsid.length() > 32) {
    server.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Укажите имя Wi-Fi сети (SSID).\"}");
    return;
  }
  String effectivePassword = newPassword;
  if (effectivePassword.length() == 0) {
    int savedIndex = findSavedNetwork(newSsid);
    if (savedIndex >= 0) effectivePassword = savedNetworks[savedIndex].password;
    else if (cfg.ssid == newSsid) effectivePassword = cfg.password;
    else {
      server.send(400, "application/json; charset=utf-8",
                  "{\"ok\":false,\"message\":\"Для новой сети введите пароль. Для сохранённой сети пароль можно оставить пустым.\"}");
      return;
    }
  }
  if (effectivePassword.length() > 63) effectivePassword = effectivePassword.substring(0, 63);
  if (!addOrUpdateSavedNetwork(newSsid, effectivePassword)) {
    server.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Память заполнена: сохранено 8 сетей.\"}");
    return;
  }
  cfg.ssid = newSsid;
  cfg.password = effectivePassword;
  if (!saveConfig()) {
    server.send(500, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Не удалось сохранить настройки. Проверьте LittleFS.\"}");
    return;
  }
  server.send(200, "application/json; charset=utf-8",
              "{\"ok\":true,\"message\":\"Настройки Wi-Fi сохранены. ESP32 перезапустится; если роутер не подключится, точка SolarPV-Setup останется доступна.\"}");
  restartAt = millis() + 1200;
}

static bool validCalendarDate(int year, int month, int day) {
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1) return false;
  static const uint8_t daysInMonth[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int maxDay = daysInMonth[month - 1];
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) maxDay = 29;
  return day <= maxDay;
}

void handleSetClock() {
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Некорректные данные времени.\"}"); return; }
  String date = String(doc["date"] | ""); String time = String(doc["time"] | "");
  if (date.length() != 10 || time.length() < 5) { server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Укажи дату и время.\"}"); return; }
  int year=date.substring(0,4).toInt(), month=date.substring(5,7).toInt(), day=date.substring(8,10).toInt();
  int hour=time.substring(0,2).toInt(), minute=time.substring(3,5).toInt(), second=time.length() >= 8 ? time.substring(6,8).toInt() : 0;
  if (!validCalendarDate(year, month, day) || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) { server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Дата или время вне допустимого диапазона.\"}"); return; }
  noInterrupts(); clockCommandYear=year; clockCommandMonth=month; clockCommandDay=day; clockCommandHour=hour; clockCommandMinute=minute; clockCommandSecond=second; clockCommandSequence++; clockCommandType=1; clockCommandPending=true; interrupts();
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"message\":\"Команда установки времени отправлена шлюзу. Дождись обновления RTC.\"}");
}

void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}


enum RemoteTextMode : uint8_t { RT_CLOSED, RT_PICKER, RT_EDIT, RT_WIFI_MENU, RT_CLOCK, RT_CLOCK_CONFIRM };

// Wire protocol shared with ESP8266_Gateway. The ESP8266 is the I2C master;
// ESP32 is the slave at 0x42. All multi-byte fields are little-endian.
static const uint8_t GATEWAY_I2C_ADDRESS = 0x42;
static const uint8_t GATEWAY_FRAME_MAGIC = 0xA7;
static const uint8_t GATEWAY_FRAME_VERSION = 1;
static const uint8_t GATEWAY_ACK_MAGIC = 0x5A;
#pragma pack(push, 1)
struct GatewayPacket {
  uint8_t magic;
  uint8_t version;
  uint8_t sequence;
  uint8_t flags;             // bit 0: RTC valid, bit 1: sensor data valid
  uint16_t year;
  uint8_t month, day, hour, minute, second;
  uint8_t sensorId;
  float voltage, current, power, shuntVoltage;
  uint16_t sensorPacketNumber;
  uint8_t crc;
};
#pragma pack(pop)
static_assert(sizeof(GatewayPacket) == 31, "GatewayPacket size mismatch");
static volatile uint8_t gatewayRxRaw[sizeof(GatewayPacket)];
static volatile uint8_t gatewayRxLength = 0;
static volatile bool gatewayRxPending = false;
static volatile uint8_t gatewayAckSequence = 0;
static volatile uint8_t gatewayAckStatus = 1;


static uint8_t gatewayCrc8(const uint8_t* data, size_t length) {
  uint8_t crc = 0;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}
static void gatewayOnReceive(int count) {
  uint8_t raw[sizeof(GatewayPacket)];
  uint8_t stored = 0;
  uint8_t received = 0;
  while (Wire.available()) {
    uint8_t b = (uint8_t)Wire.read();
    if (stored < sizeof(gatewayRxRaw)) {
      gatewayRxRaw[stored] = b;
      raw[stored] = b;
      ++stored;
    }
    if (received < 255) ++received;
  }
  gatewayRxLength = received;
  gatewayRxPending = true;

  // Prepare the acknowledgement in the receive callback, before the master
  // immediately requests it. Keep all heavier state updates in loop().
  bool valid = received == sizeof(GatewayPacket);
  if (valid) {
    GatewayPacket packet;
    memcpy(&packet, raw, sizeof(packet));
    valid = packet.magic == GATEWAY_FRAME_MAGIC &&
            packet.version == GATEWAY_FRAME_VERSION &&
            gatewayCrc8(raw, sizeof(packet) - 1) == packet.crc;
    gatewayAckSequence = packet.sequence;
  } else if (received >= 3) {
    gatewayAckSequence = raw[2];
  }
  gatewayAckStatus = valid ? 0 : 1;
}
static void gatewayOnRequest() {
  // ACK plus one-shot RTC command. The gateway de-duplicates by command sequence.
  uint8_t response[12] = { GATEWAY_ACK_MAGIC, gatewayAckSequence, gatewayAckStatus,
    (uint8_t)(clockCommandPending ? clockCommandType : 0), clockCommandSequence,
    (uint8_t)(clockCommandYear & 0xFF), (uint8_t)(clockCommandYear >> 8),
    clockCommandMonth, clockCommandDay, clockCommandHour, clockCommandMinute, clockCommandSecond };
  Wire.write(response, sizeof(response));
  if (clockCommandPending) clockCommandPending = false;
}
static void processGatewayPacket() {
  uint8_t raw[sizeof(GatewayPacket)];
  uint8_t length = 0;
  noInterrupts();
  bool pending = gatewayRxPending;
  if (pending) {
    length = gatewayRxLength;
    for (size_t i = 0; i < sizeof(raw); ++i) raw[i] = gatewayRxRaw[i];
    gatewayRxPending = false;
  }
  interrupts();
  if (pending) {
    GatewayPacket packet;
    bool valid = length == sizeof(packet);
    if (valid) {
      memcpy(&packet, raw, sizeof(packet));
      valid = packet.magic == GATEWAY_FRAME_MAGIC && packet.version == GATEWAY_FRAME_VERSION &&
              gatewayCrc8(raw, sizeof(packet) - 1) == packet.crc;
    }
    if (valid) {
      gatewayAckSequence = packet.sequence;
      gatewayAckStatus = 0;
      gatewayLastPacketMs = millis();
      gatewayTimeValid = (packet.flags & 0x01) != 0;
      gatewayYear = packet.year; gatewayMonth = packet.month; gatewayDay = packet.day;
      gatewayHour = packet.hour; gatewayMinute = packet.minute; gatewaySecond = packet.second;
      gatewaySensorValid = (packet.flags & 0x02) != 0;
      gatewaySensorId = packet.sensorId;
      gatewayVoltage = packet.voltage;
      gatewayCurrent = packet.current;
      gatewayPower = packet.power;
      gatewayShuntVoltage = packet.shuntVoltage;
      gatewaySensorPacketNumber = packet.sensorPacketNumber;
      gatewayOnline = true;
      if (millis() - lastGatewayLogMs >= 1000UL) {
        lastGatewayLogMs = millis();
        if (gatewayTimeValid) {
          Serial.printf("Gateway RTC: %04u-%02u-%02u %02u:%02u:%02u",
                        (unsigned)gatewayYear, (unsigned)gatewayMonth, (unsigned)gatewayDay,
                        (unsigned)gatewayHour, (unsigned)gatewayMinute, (unsigned)gatewaySecond);
        } else {
          Serial.print(F("Gateway RTC: unavailable"));
        }
        if (gatewaySensorValid) {
          Serial.printf(" | sensor %u: %.2f V, %.3f A, %.1f W, shunt %.3f mV, packet %u\n",
                        (unsigned)gatewaySensorId, gatewayVoltage, gatewayCurrent, gatewayPower,
                        gatewayShuntVoltage, (unsigned)gatewaySensorPacketNumber);
        } else {
          Serial.println(F(" | sensor data unavailable"));
        }
      }
    } else {
      gatewayAckStatus = 1;
      if (length >= 3) gatewayAckSequence = raw[2];
    }
  }
  if (gatewayLastPacketMs == 0 || millis() - gatewayLastPacketMs > 500UL) gatewayOnline = false;
}
static RemoteTextMode rtMode = RT_CLOSED;
static uint8_t rtPick = 0;
static int8_t rtWidget = -1;
static String rtBuffer;
static size_t rtCursor = 0;
static bool rtRussian = true, rtUpper = true;
static uint8_t rtKeyRow = 1, rtKeyCol = 0;
static bool rtSymbolsPage = false;
static bool rtWifiPassword = false;
static String rtWifiNetworks[20];
static bool rtWifiSecure[20];
static uint8_t rtWifiCount = 0, rtWifiPick = 0;
static uint8_t rtWifiModeRow = 0;
static bool rtPendingRouter = false, rtPendingAp = true;
static String rtWifiSelectedSSID;
static uint16_t rtClockYear=2026;
static uint8_t rtClockMonth=1, rtClockDay=1, rtClockHour=0, rtClockMinute=0, rtClockSecond=0, rtClockField=0;

static size_t rtBytes(const String& s, size_t p) {
  if (p >= s.length()) return 0;
  uint8_t c=(uint8_t)s[p];
  return c<0x80?1:((c&0xE0)==0xC0?2:((c&0xF0)==0xE0?3:((c&0xF8)==0xF0?4:1)));
}
static size_t rtPrev(const String& s,size_t p) {
  if(!p)return 0; p--;
  while(p>0&&(((uint8_t)s[p]&0xC0)==0x80))p--;
  return p;
}
static String rtChar(const String& s,size_t p){return s.substring(p,p+rtBytes(s,p));}
static int rtTextCount(){
  if(cfg.tftPage<1||cfg.tftPage>cfg.pageCount)return 0;
  int n=0; PageConfig& p=cfg.pages[cfg.tftPage-1];
  for(uint8_t i=0;i<p.widgetCount;i++)if(p.widgets[i].enabled&&p.widgets[i].type=="text")n++;
  return n;
}
static int rtCount(){ return (rtTextCount() > 6 ? 6 : rtTextCount()) + 1; }
static int rtIndex(uint8_t wanted){
  if(cfg.tftPage<1||cfg.tftPage>cfg.pageCount)return -1;
  PageConfig& p=cfg.pages[cfg.tftPage-1]; uint8_t n=0;
  for(uint8_t i=0;i<p.widgetCount&&n<6;i++)if(p.widgets[i].enabled&&p.widgets[i].type=="text"){
    if(n++==wanted)return i;
  }
  if(wanted==n)return -2;
  return -1;
}
static String rtKeyAt(uint8_t row, uint8_t col) {
  if (rtSymbolsPage && row < 5) {
    static const char* symbols[5] = {"0123456789","!@#$%^&*()","-_=+[]{}<>", ".,;:\'\\\"~", "/?\\\\|%&^"};
    return String(symbols[row]).substring(col, col + 1);
  }
  if (!rtSymbolsPage && row < 3) {
    static const char* enUpper[3] = {"ABCDEFGHIJ","KLMNOPQRST","UVWXYZ    "};
    static const char* enLower[3] = {"abcdefghij","klmnopqrst","uvwxyz    "};
    static const char* ruUpper[3] = {"АБВГДЕЁЖЗИ","ЙКЛМНОПРСТ","УФХЦЧШЩЪЫЬ"};
    static const char* ruLower[3] = {"абвгдеёжзи","йклмнопрст","уфхцчшщъыь"};
    String keys = rtRussian ? (rtUpper ? String(ruUpper[row]) : String(ruLower[row])) : (rtUpper ? String(enUpper[row]) : String(enLower[row]));
    size_t pos=0; for(uint8_t n=0;n<col&&pos<keys.length();n++)pos+=rtBytes(keys,pos);
    String key=rtChar(keys,pos); return key==" " ? String("") : key;
  }
  if ((!rtSymbolsPage && row == 3) || (rtSymbolsPage && row == 5)) {
    static const char* textActions[10] = {"123","SP","DEL","NL","RU","Aa","","","SAVE",""};
    static const char* symbolActions[10] = {"ABC","SP","DEL","NL","RU","Aa","","","SAVE",""};
    return rtSymbolsPage ? symbolActions[col] : textActions[col];
  }
  return String("");
}
static void rtMoveSelection(int rowDelta, int colDelta) {
  const int maxRows = rtSymbolsPage ? 6 : 4;
  for (int tries = 0; tries < maxRows * 10; ++tries) {
    int row = ((int)rtKeyRow + rowDelta + maxRows) % maxRows;
    int col = ((int)rtKeyCol + colDelta + 10) % 10;
    rtKeyRow = (uint8_t)row; rtKeyCol = (uint8_t)col;
    if (rtKeyAt(rtKeyRow, rtKeyCol).length()) return;
  }
}
static void rtDrawPicker(){
  tft.fillScreen(ST77XX_BLACK); drawCyrillicText("ТЕКСТОВЫЙ БЛОК",8,25,ST77XX_WHITE,true);
  drawCyrillicText("UP/DOWN выбор, OK ввод",8,48,0x07FF,false);
  PageConfig& p=cfg.pages[cfg.tftPage-1]; int y=82; uint8_t n=0;
  for(uint8_t i=0;i<p.widgetCount&&n<6;i++){WidgetConfig& w=p.widgets[i];if(!w.enabled||w.type!="text")continue;
    if(n==rtPick)tft.fillRoundRect(5,y-16,230,28,4,0x03EF);
    drawCyrillicText((n==rtPick?"> ":"  ")+w.label.substring(0,27),10,y+2,ST77XX_WHITE,false);y+=30;n++;
  }
  if(n==rtPick)tft.fillRoundRect(5,y-16,230,28,4,0x03EF);
  drawCyrillicText((n==rtPick?"> ":"  ")+String("Часы и дата — настроить"),10,y+2,ST77XX_WHITE,false);
  drawCyrillicText("MENU: выход",8,307,0xBDF7,false);
}
static void rtDrawEditor(){
  tft.fillScreen(ST77XX_BLACK);
  PageConfig& p=cfg.pages[cfg.tftPage-1];
  if (rtWifiPassword) drawCyrillicText("ПАРОЛЬ WI-FI",5,13,0x07FF,false);
  else { WidgetConfig& w=p.widgets[rtWidget]; drawCyrillicText(w.label.substring(0,30),5,13,0x07FF,false); }

  // A compact preview of the text being edited, with a caret at the insertion point.
  String preview=rtBuffer;
  if(rtWifiPassword){preview="";for(size_t i=0;i<rtBuffer.length();i++)preview+="*";}
  String view=preview.substring(0,rtCursor)+"_"+preview.substring(rtCursor);
  String line=""; int y=31; uint8_t lines=0;
  for(size_t pos=0;pos<view.length()&&lines<4;){
    String g=rtChar(view,pos);
    if(g=="\n"||u8g2Fonts.getUTF8Width((line+g).c_str())>228){
      drawCyrillicText(line,5,y,ST77XX_WHITE,false); line=""; y+=15; lines++;
      if(g=="\n"){pos++;continue;}
    } else line+=g;
    pos+=g.length();
  }
  if(lines<4&&line.length())drawCyrillicText(line,5,y,ST77XX_WHITE,false);

  const int x0=2, y0=96, cellW=23, cellH=29;
  const uint8_t keyboardRows = rtSymbolsPage ? 6 : 4;
  for(uint8_t row=0;row<keyboardRows;row++){
    for(uint8_t col=0;col<10;col++){
      int x=x0+col*cellW, yk=y0+row*cellH;
      String key=rtKeyAt(row,col);
      if (!key.length()) continue; // Do not draw empty keyboard cells.
      bool selected=(row==rtKeyRow&&col==rtKeyCol);
      if(selected)tft.fillRoundRect(x,yk,cellW-1,cellH-2,3,0x03EF);
      else tft.drawRoundRect(x,yk,cellW-1,cellH-2,3,0x4208);
      int tw=u8g2Fonts.getUTF8Width(key.c_str());
      int tx=x+(cellW-tw)/2;
      if(tx<x+1)tx=x+1;
      drawCyrillicText(key,tx,yk+19,selected?ST77XX_WHITE:0xBDF7,false);
    }
  }
}

static void rtWifiDrawMenu(const String& message = "") {
  tft.fillScreen(ST77XX_BLACK);
  drawCyrillicText("УПРАВЛЕНИЕ WI-FI",5,22,0x07FF,true);
  drawCyrillicText("ВВЕРХ/ВНИЗ: строка",5,47,0xBDF7,false);
  drawCyrillicText("ВЛЕВО/ВПРАВО: изменить",5,64,0xBDF7,false);

  const char* labels[2] = {"СЕТЬ Роутер", "ТОЧКА ДОСТУПА"};
  bool values[2] = {rtPendingRouter, rtPendingAp};
  for (uint8_t row = 0; row < 2; ++row) {
    int y = 105 + row * 48;
    if (row == rtWifiModeRow) tft.fillRoundRect(4,y-22,232,36,4,0x03EF);
    drawCyrillicText(labels[row],10,y,ST77XX_WHITE,false);
    drawCyrillicText(values[row] ? "[ V ]" : "[   ]",181,y,values[row]?0x07E0:0xF800,false);
  }
  if (message.length()) drawCyrillicText(message.substring(0,30),5,225,ST77XX_YELLOW,false);
  else drawCyrillicText("OK: применить  SEARCH: сети",5,225,0xBDF7,false);
  drawCyrillicText("MENU/BACK: выход",5,307,0xBDF7,false);
}
static void rtWifiScan() {
  if(!routerModeEnabled && !setupApEnabled){rtWifiDrawMenu("Сначала включи Wi-Fi (1 или 2)");return;}
  WiFi.mode(setupApEnabled && routerModeEnabled ? WIFI_AP_STA : (setupApEnabled ? WIFI_AP : WIFI_STA));
  drawCyrillicText("Поиск сетей Wi-Fi...",5,87,ST77XX_WHITE,false);
  int count=WiFi.scanNetworks(false,true); rtWifiCount=0;
  if(count>=0) for(int i=0;i<count&&rtWifiCount<20;i++){
    String ssid=WiFi.SSID(i); if(!ssid.length())continue;
    bool duplicate=false; for(uint8_t j=0;j<rtWifiCount;j++)if(rtWifiNetworks[j]==ssid)duplicate=true;
    if(duplicate)continue;
    rtWifiNetworks[rtWifiCount]=ssid;
    rtWifiSecure[rtWifiCount]=(WiFi.encryptionType(i)!=WIFI_AUTH_OPEN);
    rtWifiCount++;
  }
  WiFi.scanDelete(); rtWifiPick=0;
  rtWifiDrawMenu(rtWifiCount?("Найдено сетей: "+String(rtWifiCount)):"Сети не найдены");
}
static void rtWifiConnect(const String& ssid,const String& password) {
  if(!ssid.length()){rtWifiDrawMenu("Пустое имя сети");return;}
  if(!addOrUpdateSavedNetwork(ssid,password)){rtWifiDrawMenu("Память сетей заполнена");return;}
  cfg.ssid=ssid; cfg.password=password;
  if(!saveConfig()){rtWifiDrawMenu("Ошибка сохранения");return;}
  WiFi.mode(WIFI_AP_STA); WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.ssid.c_str(),cfg.password.c_str());
  rtMode=RT_WIFI_MENU; rtWifiDrawMenu("Подключение запущено...");
  Serial.printf("IR Wi-Fi: connecting to SSID '%s'\\n",ssid.c_str());
}
static void rtOpenWifi() {
  rtMode=RT_WIFI_MENU; rtWifiCount=0; rtWifiPick=0; rtWifiModeRow=0;
  rtPendingRouter=routerModeEnabled; rtPendingAp=setupApEnabled; rtWifiDrawMenu();
}
static void rtWifiChoose() {
  if(rtWifiPick>=rtWifiCount)return;
  rtWifiSelectedSSID=rtWifiNetworks[rtWifiPick];
  int saved=findSavedNetwork(rtWifiSelectedSSID);
  if(saved>=0){rtWifiConnect(rtWifiSelectedSSID,savedNetworks[saved].password);return;}
  if(!rtWifiSecure[rtWifiPick]){rtWifiConnect(rtWifiSelectedSSID,"");return;}
  rtBuffer="";rtCursor=0;rtWifiPassword=true;rtRussian=false;rtUpper=true;rtSymbolsPage=false;rtKeyRow=0;rtKeyCol=0;rtMode=RT_EDIT;rtDrawEditor();
}

static void rtOpen(){
  if(!rtCount()){tft.fillScreen(ST77XX_BLACK);drawCyrillicText("Нет текстовых блоков",8,35,ST77XX_WHITE,true);drawCyrillicText("Добавь блок в веб-админке",8,62,0xBDF7,false);delay(900);renderTFT();return;}
  rtMode=RT_PICKER;rtPick=0;rtDrawPicker();
}
static void rtOpenClockEditor();
static void rtEdit(){
  rtWidget=rtIndex(rtPick);if(rtWidget==-2){rtOpenClockEditor();return;}if(rtWidget<0){rtMode=RT_CLOSED;renderTFT();return;}
  rtBuffer=cfg.pages[cfg.tftPage-1].widgets[rtWidget].text;if(rtBuffer.length()>240)rtBuffer=rtBuffer.substring(0,240);
  rtCursor=rtBuffer.length();rtMode=RT_EDIT;rtKeyRow=0;rtKeyCol=0;rtUpper=true;rtRussian=true;rtSymbolsPage=false;rtDrawEditor();
}
static void rtInsert(String ch){
  if(!ch.length()||rtBuffer.length()+ch.length()>240)return;
  rtBuffer=rtBuffer.substring(0,rtCursor)+ch+rtBuffer.substring(rtCursor);rtCursor+=ch.length();rtDrawEditor();
}
static void rtSave(){
  if(rtWifiPassword){rtWifiConnect(rtWifiSelectedSSID,rtBuffer);rtWifiPassword=false;return;}
  if(rtWidget>=0&&cfg.tftPage>=1&&cfg.tftPage<=cfg.pageCount){
    cfg.pages[cfg.tftPage-1].widgets[rtWidget].text=rtBuffer;
    bool ok=saveConfig();Serial.println(ok?F("IR text block saved."):F("ERROR: text block save failed."));
  }
  rtMode=RT_CLOSED;renderTFT();
}
static void rtDrawClockConfirm(){
  tft.fillScreen(ST77XX_BLACK);
  drawCyrillicText("ПОДТВЕРДИТЬ ВРЕМЯ",5,25,0xFFE0,true);
  char date[16], time[16]; snprintf(date,sizeof(date),"%02u.%02u.%04u",(unsigned)rtClockDay,(unsigned)rtClockMonth,(unsigned)rtClockYear);
  snprintf(time,sizeof(time),"%02u:%02u:%02u",(unsigned)rtClockHour,(unsigned)rtClockMinute,(unsigned)rtClockSecond);
  drawCyrillicText(date,25,85,ST77XX_WHITE,true); drawCyrillicText(time,25,125,ST77XX_WHITE,true);
  drawCyrillicText("OK: записать в DS3231",5,195,0x07E0,false);
  drawCyrillicText("BACK: вернуться к правке",5,220,0xBDF7,false);
  drawCyrillicText("MENU: отмена",5,245,0xBDF7,false);
}
static void rtDrawClockEditor(){
  tft.fillScreen(ST77XX_BLACK);
  drawCyrillicText("УСТАНОВКА ЧАСОВ",5,22,0x07FF,true);
  char date[16], time[16]; snprintf(date,sizeof(date),"%02u.%02u.%04u",(unsigned)rtClockDay,(unsigned)rtClockMonth,(unsigned)rtClockYear);
  snprintf(time,sizeof(time),"%02u:%02u:%02u",(unsigned)rtClockHour,(unsigned)rtClockMinute,(unsigned)rtClockSecond);
  drawCyrillicText(date,25,80,rtClockField<=2?0xFFE0:ST77XX_WHITE,true);
  drawCyrillicText(time,25,120,rtClockField>=3?ST77XX_YELLOW:ST77XX_WHITE,true);
  int xPos[6]={25,55,105,25,65,105}; int yPos[6]={80,80,80,120,120,120}; int width[6]={22,22,48,22,22,22};
  tft.drawRoundRect(xPos[rtClockField]-3,yPos[rtClockField]-17,width[rtClockField],24,3,0x07FF);
  drawCyrillicText("LEFT/RIGHT: поле",5,185,0xBDF7,false); drawCyrillicText("UP/DOWN: изменить",5,207,0xBDF7,false);
  drawCyrillicText("OK: записать в RTC",5,229,0x07E0,false); drawCyrillicText("BACK: отмена",5,251,0xBDF7,false);
}
static void rtOpenClockEditor(){
  rtClockYear=gatewayTimeValid?gatewayYear:2026; rtClockMonth=gatewayTimeValid?gatewayMonth:1; rtClockDay=gatewayTimeValid?gatewayDay:1;
  rtClockHour=gatewayTimeValid?gatewayHour:0; rtClockMinute=gatewayTimeValid?gatewayMinute:0; rtClockSecond=gatewayTimeValid?gatewaySecond:0;
  rtClockField=0; rtMode=RT_CLOCK; rtDrawClockEditor();
}
static uint8_t rtDaysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month < 1 || month > 12) return 31;
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
  return days[month - 1];
}
static void rtClampClockDay() {
  uint8_t maxDay = rtDaysInMonth(rtClockYear, rtClockMonth);
  if (rtClockDay > maxDay) rtClockDay = maxDay;
}
static void rtHandleClock(uint32_t k){
  if(rtMode==RT_CLOCK_CONFIRM){
    if(k==IR_CODE_OK){
      if(!validCalendarDate(rtClockYear,rtClockMonth,rtClockDay)){rtMode=RT_CLOCK;rtDrawClockEditor();drawCyrillicText("НЕКОРРЕКТНАЯ ДАТА",5,285,ST77XX_RED,false);return;}
      noInterrupts();clockCommandYear=rtClockYear;clockCommandMonth=rtClockMonth;clockCommandDay=rtClockDay;clockCommandHour=rtClockHour;clockCommandMinute=rtClockMinute;clockCommandSecond=rtClockSecond;clockCommandSequence++;clockCommandType=1;clockCommandPending=true;interrupts();
      Serial.println(F("IR clock set command queued for RTC."));rtMode=RT_CLOSED;renderTFT();return;
    }
    if(k==IR_CODE_BACK){rtMode=RT_CLOCK;rtDrawClockEditor();return;}
    if(k==IR_CODE_MENU){rtMode=RT_CLOSED;renderTFT();return;}
    return;
  }
  if(k==IR_CODE_MENU||k==IR_CODE_BACK){rtMode=RT_CLOSED;renderTFT();return;}
  if(k==IR_CODE_LEFT){rtClockField=(rtClockField+5)%6;rtDrawClockEditor();return;}
  if(k==IR_CODE_RIGHT){rtClockField=(rtClockField+1)%6;rtDrawClockEditor();return;}
  if(k==IR_CODE_UP||k==IR_CODE_DOWN){int d=k==IR_CODE_UP?1:-1;
    if(rtClockField==0)rtClockYear=(uint16_t)constrain((int)rtClockYear+d,2000,2099);
    else if(rtClockField==1)rtClockMonth=(uint8_t)((rtClockMonth-1+d+12)%12+1);
    else if(rtClockField==2){int maxDay=rtDaysInMonth(rtClockYear,rtClockMonth);rtClockDay=(uint8_t)((rtClockDay-1+d+maxDay)%maxDay+1);}
    else if(rtClockField==3)rtClockHour=(uint8_t)((rtClockHour+24+d)%24);
    else if(rtClockField==4)rtClockMinute=(uint8_t)((rtClockMinute+60+d)%60);
    else rtClockSecond=(uint8_t)((rtClockSecond+60+d)%60);
    rtClampClockDay();
    rtDrawClockEditor();return;
  }
  if(k==IR_CODE_OK){
    if(!validCalendarDate(rtClockYear,rtClockMonth,rtClockDay)){drawCyrillicText("НЕКОРРЕКТНАЯ ДАТА",5,285,ST77XX_RED,false);return;}
    rtMode=RT_CLOCK_CONFIRM;rtDrawClockConfirm();return;
  }
}

static void rtHandle(uint32_t k){
  if(rtMode==RT_CLOCK || rtMode==RT_CLOCK_CONFIRM){rtHandleClock(k);return;}
  if(rtMode==RT_PICKER){
    uint8_t n=rtCount();if(!n){rtMode=RT_CLOSED;renderTFT();return;}
    if(k==IR_CODE_UP){rtPick=rtPick?rtPick-1:n-1;rtDrawPicker();}
    else if(k==IR_CODE_DOWN){rtPick=(rtPick+1)%n;rtDrawPicker();}
    else if(k==IR_CODE_OK)rtEdit();
    else if(k==IR_CODE_MENU||k==IR_CODE_BACK){rtMode=RT_CLOSED;renderTFT();}
    return;
  }
  if(rtMode==RT_WIFI_MENU){
    if(k==IR_CODE_MENU||k==IR_CODE_BACK){rtMode=RT_CLOSED;renderTFT();return;}
    if(k==IR_CODE_UP){rtWifiModeRow=rtWifiModeRow?0:1;rtWifiDrawMenu();return;}
    if(k==IR_CODE_DOWN){rtWifiModeRow=(rtWifiModeRow+1)%2;rtWifiDrawMenu();return;}
    if(k==IR_CODE_LEFT||k==IR_CODE_RIGHT){
      if(rtWifiModeRow==0)rtPendingRouter=!rtPendingRouter;else rtPendingAp=!rtPendingAp;
      rtWifiDrawMenu();return;
    }
    if(k==IR_CODE_OK){
      routerModeEnabled=rtPendingRouter;setupApEnabled=rtPendingAp;applyWiFiMode();
      rtWifiDrawMenu("Настройки Wi-Fi применены");return;
    }
    if(k==IR_CODE_1){rtPendingRouter=!rtPendingRouter;rtWifiDrawMenu();return;}
    if(k==IR_CODE_2){rtPendingAp=!rtPendingAp;rtWifiDrawMenu();return;}
    if(k==IR_CODE_3){rtPendingRouter=false;rtPendingAp=false;rtWifiDrawMenu();return;}
    if(k==IR_CODE_SEARCH){rtWifiScan();return;}
    if(rtWifiCount && k==IR_CODE_OK){rtWifiChoose();return;}
    return;
  }
  if(rtMode!=RT_EDIT)return;
  if(k==IR_CODE_MENU){rtMode=RT_CLOSED;rtWifiPassword=false;renderTFT();return;}
  if(k==IR_CODE_SEARCH){rtSymbolsPage=!rtSymbolsPage;rtKeyRow=0;rtKeyCol=0;rtDrawEditor();return;}
  if(k==IR_CODE_0){rtInsert("0");return;} if(k==IR_CODE_1){rtInsert("1");return;} if(k==IR_CODE_2){rtInsert("2");return;} if(k==IR_CODE_3){rtInsert("3");return;} if(k==IR_CODE_4){rtInsert("4");return;} if(k==IR_CODE_5){rtInsert("5");return;} if(k==IR_CODE_6){rtInsert("6");return;} if(k==IR_CODE_7){rtInsert("7");return;} if(k==IR_CODE_8){rtInsert("8");return;} if(k==IR_CODE_9){rtInsert("9");return;}
  if(k==IR_CODE_UP){rtMoveSelection(-1,0);rtDrawEditor();return;}
  if(k==IR_CODE_DOWN){rtMoveSelection(1,0);rtDrawEditor();return;}
  if(k==IR_CODE_LEFT){rtMoveSelection(0,-1);rtDrawEditor();return;}
  if(k==IR_CODE_RIGHT){rtMoveSelection(0,1);rtDrawEditor();return;}
  if(k==IR_CODE_OK){
    String key=rtKeyAt(rtKeyRow,rtKeyCol);
    if(key=="123"||key=="ABC"){rtSymbolsPage=!rtSymbolsPage;rtKeyRow=0;rtKeyCol=0;rtDrawEditor();}
    else if(key=="SP")rtInsert(" ");
    else if(key=="DEL"){if(rtCursor){size_t q=rtPrev(rtBuffer,rtCursor);rtBuffer.remove(q,rtCursor-q);rtCursor=q;rtDrawEditor();}}
    else if(key=="NL")rtInsert("\n");
    else if(key=="RU"){rtRussian=!rtRussian;rtDrawEditor();}
    else if(key=="Aa"){rtUpper=!rtUpper;rtDrawEditor();}
    else if(key=="SAVE")rtSave();
    else rtInsert(key);
    return;
  }
  // Convenient remote shortcuts remain available; the on-screen keys are the primary controls.
  if(k==IR_CODE_BACK){if(rtCursor){size_t q=rtPrev(rtBuffer,rtCursor);rtBuffer.remove(q,rtCursor-q);rtCursor=q;rtDrawEditor();}return;}
  if(k==IR_CODE_START){rtSave();return;}
}

// Change the physical TFT page without writing flash on every button press.
static void showTFTPage(uint8_t page) {
  if (cfg.pageCount == 0) return;
  if (page < 1) page = 1;
  if (page > cfg.pageCount) page = cfg.pageCount;
  if (cfg.tftPage == page) return;
  cfg.tftPage = page;
  renderTFT();
  Serial.printf("IR remote: TFT page %u/%u\n", cfg.tftPage, cfg.pageCount);
}

static void handleIRRemote() {
  const uint32_t code = IR_RemoteUpdate();
  if (code == 0) return;
  if (rtMode != RT_CLOSED) { rtHandle(code); return; }
  if (hasWifiControlOnCurrentPage() && (code == IR_CODE_UP || code == IR_CODE_DOWN || code == IR_CODE_OK)) { handleInlineWifiControl(code); return; }
  if (code == IR_CODE_SETUP) rtOpenWifi();
  else if (code == IR_CODE_MENU) rtOpen();
  else if (code == IR_CODE_LEFT || code == IR_CODE_BACK) showTFTPage(cfg.tftPage > 1 ? cfg.tftPage - 1 : cfg.pageCount);
  else if (code == IR_CODE_RIGHT || code == IR_CODE_FORWARD) showTFTPage(cfg.tftPage < cfg.pageCount ? cfg.tftPage + 1 : 1);
  else if (code == IR_CODE_TITLE || code == IR_CODE_BEGINNING) showTFTPage(1);
  else if (code == IR_CODE_END) showTFTPage(cfg.pageCount);
  else if (code == IR_CODE_DISPLAY) showTFTPage(cfg.tftPage < cfg.pageCount ? cfg.tftPage + 1 : 1);
  else if (code == IR_CODE_1) showTFTPage(1);
  else if (code == IR_CODE_2) showTFTPage(2);
  else if (code == IR_CODE_3) showTFTPage(3);
  else if (code == IR_CODE_4) showTFTPage(4);
  else Serial.printf("IR remote: received code 0x%08lX (no action assigned yet)\n", static_cast<unsigned long>(code));
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println(F("Solar_PV ESP32 Admin MVP"));

  // On a brand-new ESP32 the LittleFS partition is blank, so mounting without
  // formatting fails. Try a normal mount first to preserve existing settings;
  // only if that fails, format the LittleFS partition and mount it again.
  fsReady = LittleFS.begin(false);
  if (!fsReady) {
    Serial.println(F("WARNING: LittleFS mount failed; attempting one-time filesystem format."));
    fsReady = LittleFS.begin(true);
    if (fsReady) Serial.println(F("LittleFS formatted and mounted successfully."));
    else Serial.println(F("ERROR: LittleFS mount/format failed. Settings cannot be saved."));
  }

  loadConfig();
  // Always start on the main TFT page after power-up or restart.
  // Keep the saved page setting untouched; it only affects the current session.
  cfg.tftPage = 1;

  tft.init(240, 320);
  tft.setRotation(0);
  u8g2Fonts.begin(tft);
  u8g2Fonts.setFontMode(1);
  u8g2Fonts.setFontDirection(0);
  tft.fillScreen(ST77XX_BLACK);
  drawCyrillicText("СОЛАР PV", 8, 28, ST77XX_WHITE, true);
  drawCyrillicText("Запуск админки...", 8, 48, 0xBDF7, false);

  // Start the ESP32 as the I2C slave before Wi-Fi and the web server.
  Wire.onReceive(gatewayOnReceive);
  Wire.onRequest(gatewayOnRequest);
  if (Wire.begin(GATEWAY_I2C_ADDRESS, 21, 22, 100000)) Serial.println(F("I2C gateway slave ready at 0x42 (SDA=21, SCL=22)."));
  else Serial.println(F("ERROR: I2C gateway slave initialization failed."));

  connectWiFiAtBoot();
  setupWebServer();
  renderTFT();
  IR_RemoteBegin();
  Serial.println(F("IR remote ready on GPIO27 (NEC)."));
}

void loop() {
  handleIRRemote();
  processGatewayPacket();
  if (rtMode == RT_CLOSED && millis() - lastIndicatorDraw >= 250UL) {
    lastIndicatorDraw = millis();
    drawTFTIndicators();
  }
  if (webStarted) server.handleClient();

  if (restartAt != 0 && (long)(millis() - restartAt) >= 0) {
    delay(100);
    ESP.restart();
  }

  static uint32_t lastWifiControlRefresh = 0;
  if (wifiControlApplyPending && rtMode == RT_CLOSED && millis() - lastWifiControlRefresh >= 1000UL) {
    lastWifiControlRefresh = millis();
    refreshWifiControlWidget();
  }

  if (wifiControlApplyPending && (long)(millis() - wifiControlApplyAt) >= 0) {
    routerModeEnabled = wifiControlPendingRouter;
    setupApEnabled = wifiControlPendingAp;
    applyWiFiMode();
    wifiControlApplyPending = false;
    refreshWifiControlWidget();
    Serial.println(F("Inline Wi-Fi control applied after 5 seconds."));
  }

  // Runtime switches are authoritative; never auto-enable a disabled interface.
  if (routerModeEnabled && WiFi.status() == WL_CONNECTED) disconnectedSince = 0;

  static uint8_t lastClockSecond = 255;
  if (rtMode == RT_CLOSED && gatewayTimeValid && gatewaySecond != lastClockSecond && cfg.tftPage >= 1 && cfg.tftPage <= cfg.pageCount) {
    lastClockSecond = gatewaySecond;
    PageConfig& cp = cfg.pages[cfg.tftPage - 1];
    for (uint8_t i = 0; i < cp.widgetCount; ++i) {
      if (cp.widgets[i].enabled && cp.widgets[i].type == "clock") updateClockWidget(cp.widgets[i]);
    }
  }

  // Refresh only Wi-Fi widget regions, not the entire TFT page.
  if (millis() - lastStatusDraw >= 15000UL) {
    lastStatusDraw = millis();
    bool hasDynamicWidget = false;
    if (cfg.tftPage >= 1 && cfg.tftPage <= cfg.pageCount) {
      PageConfig& page = cfg.pages[cfg.tftPage - 1];
      for (uint8_t i = 0; i < page.widgetCount; i++) {
        if (page.widgets[i].enabled && page.widgets[i].type == "wifiStatus") drawWidget(page.widgets[i]);
      }
    }
    (void)hasDynamicWidget;
  }
}
