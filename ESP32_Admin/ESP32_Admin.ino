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
#include "EnergyCounter.h"
#include "EnergyEepromTransport.h"
#include "EnergyEepromStore.h"
#include "EnergyCalendarStore.h"

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

bool wifiStatusInlineActive = false;
bool wifiStatusPendingRouter = true;
bool wifiStatusPendingAp = true;
uint8_t wifiStatusSelectedRow = 0;
static bool hasWifiStatusWidgetOnCurrentPage() {
  if (cfg.tftPage < 1 || cfg.tftPage > cfg.pageCount) return false;
  PageConfig& page = cfg.pages[cfg.tftPage - 1];
  for (uint8_t i = 0; i < page.widgetCount; ++i) {
    const WidgetConfig& widget = page.widgets[i];
    if (!widget.enabled) continue;
    for (uint8_t e = 0; e < widget.elementCount; ++e) {
      const WidgetElementConfig& element = widget.elements[e];
      if (element.kind == ELEMENT_WIFI_STATUS && (element.metric == 0 || element.metric == 4)) return true;
    }
  }
  return false;
}
static void refreshWifiStatusWidget() {
  if (cfg.tftPage < 1 || cfg.tftPage > cfg.pageCount) return;
  PageConfig& page = cfg.pages[cfg.tftPage - 1];
  for (uint8_t i = 0; i < page.widgetCount; ++i) {
    const WidgetConfig& widget = page.widgets[i];
    if (!widget.enabled) continue;
    bool hasStatus = false;
    for (uint8_t e = 0; e < widget.elementCount; ++e) {
      const WidgetElementConfig& element = widget.elements[e];
      if (element.kind == ELEMENT_WIFI_STATUS && (element.metric == 0 || element.metric == 4)) { hasStatus = true; break; }
    }
    if (hasStatus) updateWidgetIncremental(widget, cfg.tftPage - 1, i);
  }
}
static void handleInlineWifiStatus(uint32_t code) {
  if (code == IR_CODE_BACK || code == IR_CODE_MENU) {
    wifiStatusInlineActive = false;
    renderTFT();
    return;
  }

  // Both vertical arrows cycle through the two service rows.
  if (code == IR_CODE_UP || code == IR_CODE_DOWN) {
    wifiStatusSelectedRow = (wifiStatusSelectedRow + 1) % 2;
    refreshWifiStatusWidget();
    return;
  }

  // Either horizontal arrow toggles the selected service.
  // LEFT and RIGHT intentionally have identical toggle behavior.
  if (code == IR_CODE_LEFT || code == IR_CODE_RIGHT) {
    if (wifiStatusSelectedRow == 0) wifiStatusPendingRouter = !wifiStatusPendingRouter;
    else wifiStatusPendingAp = !wifiStatusPendingAp;
    refreshWifiStatusWidget();
    return;
  }

  if (code == IR_CODE_OK) {
    const bool oldRouterDefault = cfg.defaultRouterMode;
    const bool oldApDefault = cfg.defaultSetupAp;
    cfg.defaultRouterMode = wifiStatusPendingRouter;
    cfg.defaultSetupAp = wifiStatusPendingAp;
    if (saveConfig()) {
      routerModeEnabled = wifiStatusPendingRouter;
      setupApEnabled = wifiStatusPendingAp;
      applyWiFiMode();
      wifiStatusInlineActive = false;
      renderTFT();
    } else {
      cfg.defaultRouterMode = oldRouterDefault;
      cfg.defaultSetupAp = oldApDefault;
      Serial.println(F("ERROR: Wi-Fi mode settings could not be saved; current services were not changed."));
      refreshWifiStatusWidget();
    }
    return;
  }

  refreshWifiStatusWidget();
}

static const uint8_t MAX_STRING_SENSORS = MAX_SENSOR_NODES;
static volatile uint8_t clockCommandSequence = 0;
static volatile uint8_t clockCommandType = 0;
static volatile uint16_t clockCommandYear = 2026;
static volatile uint8_t clockCommandMonth = 1, clockCommandDay = 1, clockCommandHour = 0, clockCommandMinute = 0, clockCommandSecond = 0;
static volatile bool clockCommandPending = false;
struct CalibrationCommand {
  uint8_t sensorId;
  float currentFactor;
  float voltageFactor;
};
static CalibrationCommand calibrationQueue[MAX_STRING_SENSORS + 1] = {};
static volatile uint8_t calibrationQueueHead = 0;
static volatile uint8_t calibrationQueueTail = 0;
static CalibrationCommand calibrationCommandInFlight = {};
static volatile bool calibrationCommandPending = false;
static volatile uint8_t calibrationCommandSequence = 0;
static bool otaConfirmActive = false;

static void queueCalibrationCommand(uint8_t index) {
  // Each sensor node has its own calibration command and independent ID.
  if (index >= MAX_STRING_SENSORS) return;
  uint8_t next = (uint8_t)((calibrationQueueHead + 1) % (MAX_STRING_SENSORS + 1));
  if (next == calibrationQueueTail) calibrationQueueTail = (uint8_t)((calibrationQueueTail + 1) % (MAX_STRING_SENSORS + 1));
  calibrationQueue[calibrationQueueHead].sensorId = index + 1;
  calibrationQueue[calibrationQueueHead].currentFactor = cfg.currentCalibration[index];
  calibrationQueue[calibrationQueueHead].voltageFactor = cfg.voltageCalibration[index];
  calibrationQueueHead = next;
}
static void queueChangedCalibrations(const float oldCurrent[MAX_STRING_SENSORS], const float oldVoltage[MAX_STRING_SENSORS]) {
  for (uint8_t i = 0; i < MAX_STRING_SENSORS; ++i) {
    if (fabsf(cfg.currentCalibration[i] - oldCurrent[i]) > 0.0005f ||
        fabsf(cfg.voltageCalibration[i] - oldVoltage[i]) > 0.0005f)
      queueCalibrationCommand(i);
  }
}

// ESP8266 gateway telemetry arrives as a checked I2C frame.
bool gatewayOnline = false;
bool gatewayTimeValid = false;
uint32_t gatewayLastPacketMs = 0;
uint16_t gatewayYear = 0;
uint8_t gatewayMonth = 0, gatewayDay = 0, gatewayHour = 0, gatewayMinute = 0, gatewaySecond = 0;
uint8_t gatewaySensorId = 0;
float gatewayVoltage = 0.0f, gatewayCurrent = 0.0f, gatewayPower = 0.0f;
uint16_t gatewaySensorPacketNumber = 0;
bool gatewaySensorValid = false;

// Latest values for each ESP-NOW sensor ID (sensorId 1 maps to String 1).
static const uint16_t MAX_TRACKED_STRING_IDS = 255;
bool stringSensorValid[MAX_TRACKED_STRING_IDS] = {};
uint32_t stringSensorLastSeenMs[MAX_TRACKED_STRING_IDS] = {};
float stringSensorVoltage[MAX_TRACKED_STRING_IDS] = {};
float stringSensorCurrent[MAX_TRACKED_STRING_IDS] = {};
float stringSensorPower[MAX_TRACKED_STRING_IDS] = {};
float activeCurrentCalibration[MAX_STRING_SENSORS] = {};
float activeVoltageCalibration[MAX_STRING_SENSORS] = {};
bool activeCalibrationValid[MAX_STRING_SENSORS] = {};
static const uint32_t STRING_SENSOR_TIMEOUT_MS = 3500UL;

static uint32_t lastIndicatorDraw = 0;
static uint32_t lastGatewayLogMs = 0;
bool webStarted = false;
unsigned long restartAt = 0;

// Shared asynchronous Wi-Fi scan state. HTTP handlers never wait for the scan.
enum WifiScanOwner : uint8_t { WIFI_SCAN_NONE, WIFI_SCAN_WEB, WIFI_SCAN_REMOTE };
static bool wifiScanActive = false, wifiScanReady = false, wifiScanFailed = false;
static WifiScanOwner wifiScanOwner = WIFI_SCAN_NONE;
static uint8_t wifiScanCount = 0;
static String wifiScanSsids[20];
static int wifiScanRssi[20];
static bool wifiScanSecure[20];
static bool startWiFiScan(WifiScanOwner owner);
static void updateWiFiScan();








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























String makeConfigJson() {
  String j = "{";
  j += "\"title\":\"" + jsonEscape(cfg.title) + "\",";
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
  const bool routerConnected = WiFi.status() == WL_CONNECTED;
  const String routerIp = routerConnected ? WiFi.localIP().toString() : "—";
  const String apIp = apActive ? WiFi.softAPIP().toString() : "—";
  j += "\"wifiInfo\":{\"connected\":" + String(routerConnected ? "true" : "false") +
       ",\"rssi\":" + String(routerConnected ? WiFi.RSSI() : 0) +
       ",\"routerIp\":\"" + jsonEscape(routerIp) +
       "\",\"apActive\":" + String(apActive ? "true" : "false") +
       ",\"apIp\":\"" + jsonEscape(apIp) + "\"},";
  j += "\"gatewayTelemetry\":{\"valid\":" + String(gatewayOnline && gatewaySensorValid ? "true" : "false") +
       ",\"voltage\":" + String(gatewayVoltage, 3) + ",\"current\":" + String(gatewayCurrent, 4) +
       ",\"power\":" + String(gatewayPower, 2) + "},";
  char totalEnergyText[24];
  snprintf(totalEnergyText, sizeof(totalEnergyText), "%llu", (unsigned long long)energyCounter.totalWh());
  j += "\"totalEnergyWh\":" + String(totalEnergyText) + ",";
  j += "\"energyCounterReady\":" + String(energyEepromStore.ready() ? "true" : "false") + ",";
  j += "\"stringTelemetry\":[";
  bool firstTelemetry = true;
  for (uint16_t id = 1; id <= MAX_TRACKED_STRING_IDS; ++id) {
    if (!energyCounter.detected((uint8_t)id)) continue;
    const uint16_t index = id - 1U;
    const bool fresh = stringSensorValid[index] &&
                       millis() - stringSensorLastSeenMs[index] <= STRING_SENSOR_TIMEOUT_MS;
    if (!firstTelemetry) j += ",";
    firstTelemetry = false;
    j += "{\"id\":" + String(id) + ",\"valid\":" + String(fresh ? "true" : "false") +
         ",\"voltage\":" + String(stringSensorVoltage[index], 3) +
         ",\"current\":" + String(stringSensorCurrent[index], 4) +
         ",\"power\":" + String(stringSensorPower[index], 2) +
         ",\"energyWh\":" + String(energyCounter.stringWh((uint8_t)id)) + "}";
  }
  j += "],";
  j += "\"activeCurrentCalibration\":[";
  for (uint8_t i = 0; i < MAX_STRING_SENSORS; ++i) { if (i) j += ","; j += String(activeCurrentCalibration[i], 3); }
  j += "],\"activeVoltageCalibration\":[";
  for (uint8_t i = 0; i < MAX_STRING_SENSORS; ++i) { if (i) j += ","; j += String(activeVoltageCalibration[i], 3); }
  j += "],\"activeCalibrationValid\":[";
  for (uint8_t i = 0; i < MAX_STRING_SENSORS; ++i) { if (i) j += ","; bool fresh = activeCalibrationValid[i] && stringSensorValid[i] && millis() - stringSensorLastSeenMs[i] <= STRING_SENSOR_TIMEOUT_MS; j += fresh ? "true" : "false"; }
  j += "],";
  j += "\"currentCalibration\":[";
  for (uint8_t i = 0; i < MAX_STRING_SENSORS; ++i) { if (i) j += ","; j += String(cfg.currentCalibration[i], 3); }
  j += "],\"voltageCalibration\":[";
  for (uint8_t i = 0; i < MAX_STRING_SENSORS; ++i) { if (i) j += ","; j += String(cfg.voltageCalibration[i], 3); }
  j += "],";
  j += "\"tftPage\":" + String(cfg.tftPage) + ",";
  j += "\"pageCount\":" + String(cfg.pageCount) + ",\"pages\":[";
  for (uint8_t p = 0; p < cfg.pageCount; p++) {
    if (p) j += ",";
    const PageConfig& page = cfg.pages[p];
    j += "{\"title\":\"" + jsonEscape(page.title) + "\",\"showTitle\":" + String(page.showTitle ? "true" : "false") + ",\"widgetCount\":" + String(page.widgetCount) + ",\"widgets\":[";
    for (uint8_t i = 0; i < page.widgetCount; i++) {
      if (i) j += ",";
      const WidgetConfig& w = page.widgets[i];
      j += "{\"label\":\"" + jsonEscape(w.label) + "\",\"showHeading\":" + String(w.showHeading ? "true" : "false") + ",\"showBorder\":" + String(w.showBorder ? "true" : "false") + ",";
      j += "\"headingSize\":" + String(w.headingSize) + ",\"contentSize\":" + String(w.contentSize) + ",\"rowSpacing\":" + String(w.rowSpacing) + ",\"headingLetterSpacing\":" + String((int)w.headingLetterSpacing) + ",\"contentLetterSpacing\":" + String((int)w.contentLetterSpacing) + ",\"locked\":" + String(w.locked ? "true" : "false") + ",\"headingAlign\":" + String(w.headingAlign) + ",\"contentAlign\":" + String(w.contentAlign) + ",\"headingColor\":" + String(w.headingColor) + ",\"contentColor\":" + String(w.contentColor) + ",\"backgroundColor\":" + String(w.backgroundColor) + ",";
      j += "\"x\":" + String(w.x) + ",\"y\":" + String(w.y) + ",\"w\":" + String(w.w) + ",\"h\":" + String(w.h) + ",";
      j += "\"enabled\":" + String(w.enabled ? "true" : "false") + ",\"elementCount\":" + String(w.elementCount) + ",\"elements\":[";
      for (uint8_t e = 0; e < w.elementCount && e < MAX_WIDGET_ELEMENTS; ++e) {
        const WidgetElementConfig& element = w.elements[e];
        if (e) j += ",";
        j += "{\"label\":\"" + jsonEscape(element.label) + "\",\"kind\":" + String(element.kind) + ",\"sourceId\":" + String(element.sourceId) +
             ",\"metric\":" + String(element.metric) + ",\"fontSize\":" + String(element.fontSize) +
             ",\"color\":" + String(element.color) + "}";
      }
      j += "]}";
    }
    j += "]}";
  }
  j += "]}";
  j.remove(j.length() - 1); j += ",\"wifiMode\":{\"router\":" + String(routerModeEnabled ? "true" : "false") + ",\"ap\":" + String(setupApEnabled ? "true" : "false") + ",\"apActive\":" + String(apActive ? "true" : "false") + ",\"defaultRouter\":" + String(cfg.defaultRouterMode ? "true" : "false") + ",\"defaultAp\":" + String(cfg.defaultSetupAp ? "true" : "false") + "}}";
  return j;
}

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleApiConfig() {
  server.send(200, "application/json; charset=utf-8", makeConfigJson());
}

String makeEnergyHistoryJson() {
  const uint16_t detectedStrings = energyCounter.detectedCount();
  const uint16_t recordBytes = (uint16_t)(9U + (uint16_t)detectedStrings * 4U);
  const uint16_t estimatedArchiveDays = detectedStrings && recordBytes <= 1024U ? (uint16_t)(1024U / recordBytes) : 0;
  String j = "{\"ready\":" + String(energyCalendarStore.ready() ? "true" : "false") +
             ",\"storageLimited\":" + String(energyCalendarStore.storageLimited() ? "true" : "false") +
             ",\"detectedStrings\":" + String(detectedStrings) +
             ",\"checkpointStringCapacity\":125,\"estimatedArchiveDays\":" + String(estimatedArchiveDays) +
             ",\"currentDate\":" + String(energyCalendarStore.currentDate()) + ",\"current\":[";
  bool first = true;
  uint64_t currentTotal = 0;
  for (uint16_t id = 1; id <= 255; ++id) {
    const uint32_t wh = energyCalendarStore.currentDayWh((uint8_t)id);
    if (!wh) continue;
    if (!first) j += ",";
    first = false;
    j += "{\"id\":" + String(id) + ",\"wh\":" + String(wh) + "}";
    currentTotal += wh;
  }
  char totalText[24];
  snprintf(totalText, sizeof(totalText), "%llu", (unsigned long long)currentTotal);
  j += "],\"currentTotalWh\":" + String(totalText) + ",\"days\":[";
  const uint8_t count = energyCalendarStore.archivedDayCount();
  for (uint8_t dayIndex = 0; dayIndex < count; ++dayIndex) {
    if (dayIndex) j += ",";
    const uint32_t date = energyCalendarStore.archivedDate(dayIndex);
    char dateText[11];
    snprintf(dateText, sizeof(dateText), "%04lu-%02lu-%02lu",
             (unsigned long)(date / 10000UL), (unsigned long)((date / 100UL) % 100UL),
             (unsigned long)(date % 100UL));
    j += "{\"date\":\"" + String(dateText) + "\",\"dateKey\":" + String(date) + ",\"totalWh\":\"";
    uint64_t dayTotal = 0;
    for (uint16_t id = 1; id <= 255; ++id) dayTotal += energyCalendarStore.archivedDayWh(dayIndex, (uint8_t)id);
    char dayTotalText[24];
    snprintf(dayTotalText, sizeof(dayTotalText), "%llu", (unsigned long long)dayTotal);
    j += String(dayTotalText) + "\",\"strings\":[";
    bool firstString = true;
    for (uint16_t id = 1; id <= 255; ++id) {
      const uint32_t wh = energyCalendarStore.archivedDayWh(dayIndex, (uint8_t)id);
      if (!wh) continue;
      if (!firstString) j += ",";
      firstString = false;
      j += "{\"id\":" + String(id) + ",\"wh\":" + String(wh) + "}";
    }
    j += "]}";
  }
  j += "]}";
  return j;
}

void handleApiEnergyHistory() {
  server.send(200, "application/json; charset=utf-8", makeEnergyHistoryJson());
}

static bool startWiFiScan(WifiScanOwner owner) {
  if (wifiScanActive) return true;
  WiFi.scanDelete();
  wifiScanReady = false;
  wifiScanFailed = false;
  wifiScanCount = 0;
  wifiScanOwner = owner;
  int result = WiFi.scanNetworks(true, true); // async=true, include hidden SSIDs
  if (result == WIFI_SCAN_FAILED) {
    wifiScanFailed = true;
    wifiScanReady = true;
    wifiScanOwner = WIFI_SCAN_NONE;
    return false;
  }
  wifiScanActive = true;
  return true;
}

static String makeWiFiScanStatusJson() {
  if (wifiScanActive) return "{\"ok\":true,\"status\":\"scanning\"}";
  if (!wifiScanReady) return "{\"ok\":true,\"status\":\"idle\"}";
  if (wifiScanFailed) return "{\"ok\":false,\"status\":\"failed\",\"message\":\"Не удалось выполнить сканирование Wi-Fi.\"}";
  String json = "{\"ok\":true,\"status\":\"complete\",\"networks\":[";
  for (uint8_t i = 0; i < wifiScanCount; ++i) {
    if (i) json += ",";
    json += "{\"ssid\":\"" + jsonEscape(wifiScanSsids[i]) + "\",";
    json += "\"rssi\":" + String(wifiScanRssi[i]) + ",";
    json += "\"secure\":" + String(wifiScanSecure[i] ? "true" : "false") + "}";
  }
  json += "]}";
  return json;
}

void handleWiFiScanStatus() {
  server.send(200, "application/json; charset=utf-8", makeWiFiScanStatusJson());
}

void handleSaveDisplay() {
  DynamicJsonDocument doc(65536);
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Некорректный JSON макета.\"}");
    return;
  }
  JsonArray sourcePages = doc["pages"].as<JsonArray>();
  uint8_t requestedCount = clampInt(doc["pageCount"] | sourcePages.size(), 1, MAX_PAGES);
  if (sourcePages.isNull() || sourcePages.size() < requestedCount) {
    server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"В конфигурации не хватает страниц.\"}");
    return;
  }
  if (!fsReady) {
    server.send(503, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"LittleFS недоступна. Конфигурация не изменена.\"}");
    return;
  }
  if (doc.containsKey("title")) cfg.title = sanitizeTitle(String(doc["title"] | "SOLAR PV"));
  float oldCurrentCalibration[MAX_STRING_SENSORS], oldVoltageCalibration[MAX_STRING_SENSORS];
  memcpy(oldCurrentCalibration, cfg.currentCalibration, sizeof(oldCurrentCalibration));
  memcpy(oldVoltageCalibration, cfg.voltageCalibration, sizeof(oldVoltageCalibration));
  JsonArray currentCal = doc["currentCalibration"].as<JsonArray>();
  JsonArray voltageCal = doc["voltageCalibration"].as<JsonArray>();
  for (uint8_t i = 0; i < MAX_STRING_SENSORS; ++i) {
    if (currentCal.size() > i) cfg.currentCalibration[i] = constrain(currentCal[i].as<float>(), 0.001f, 99.999f);
    if (voltageCal.size() > i) cfg.voltageCalibration[i] = constrain(voltageCal[i].as<float>(), 0.001f, 99.999f);
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
      w.label = sanitizeTitle(String(source["label"] | "Блок"));
      w.headingSize = clampInt(source["headingSize"] | 1, 0, 5); w.contentSize = clampInt(source["contentSize"] | 2, 0, 5);
      w.rowSpacing = clampInt(source["rowSpacing"] | 4, 0, 20);
      w.headingLetterSpacing = clampInt(source["headingLetterSpacing"] | 0, -2, 8);
      w.contentLetterSpacing = clampInt(source["contentLetterSpacing"] | 0, -2, 8);
      JsonArray sourceElements = source["elements"].as<JsonArray>();
      w.elementCount = clampInt(sourceElements.size(), 0, MAX_WIDGET_ELEMENTS);
      for (uint8_t e = 0; e < w.elementCount; ++e) {
        JsonObject sourceElement = sourceElements[e].as<JsonObject>();
        WidgetElementConfig& element = w.elements[e];
        element.kind = clampInt(sourceElement["kind"] | ELEMENT_SENSOR, ELEMENT_SENSOR, ELEMENT_TEXT);
        element.sourceId = clampInt(sourceElement["sourceId"] | 1, 0, 11);
        element.metric = clampInt(sourceElement["metric"] | 0, 0, 127);
        if (element.kind == ELEMENT_WIFI_CONTROL) { element.kind = ELEMENT_WIFI_STATUS; element.metric = 4; }
        element.fontSize = clampInt(sourceElement["fontSize"] | 2, 0, 5);
        element.color = clampInt(sourceElement["color"] | 3, 0, 5);
        String elementLabel = String(sourceElement["label"] | "");
        if (sourceElement["kind"].as<uint8_t>() == ELEMENT_WIFI_CONTROL) elementLabel = "Состояние точки";
        element.label = sanitizeTitle(elementLabel);
      }
      w.locked = source["locked"] | false; w.showHeading = source["showHeading"] | true; w.showBorder = source["showBorder"] | true;
      w.headingAlign = clampInt(source["headingAlign"] | 0, 0, 2); w.contentAlign = clampInt(source["contentAlign"] | 0, 0, 2);
      w.headingColor = clampInt(source["headingColor"] | 0, 0, 5); w.contentColor = clampInt(source["contentColor"] | 3, 0, 5); w.backgroundColor = clampInt(source["backgroundColor"] | 0, 0, 6);
      w.w = clampInt(source["w"] | 106, 40, 240);
      w.h = clampInt(source["h"] | 58, 36, 250);
      w.x = clampInt(source["x"] | 8, 0, 240 - w.w);
      w.y = clampInt(source["y"] | 55, 40, 290 - w.h);
      w.enabled = source["enabled"] | true;
    }
  }
  cfg.tftPage = clampInt(cfg.tftPage, 1, cfg.pageCount);
  bool ok = saveConfig();
  if (!ok) {
    loadConfig();
    renderTFT();
    server.send(500, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Не удалось сохранить конфигурацию. Загружена предыдущая версия.\"}");
    return;
  }
  queueChangedCalibrations(oldCurrentCalibration, oldVoltageCalibration);
  renderTFT();
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"message\":\"Макет страниц сохранён и применён.\"}");
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
  const uint8_t previousTftPage = cfg.tftPage;
  cfg.tftPage = page;
  if (!saveConfig()) {
    cfg.tftPage = previousTftPage;
    server.send(500, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Не удалось сохранить страницу в LittleFS. Предыдущая страница восстановлена.\"}");
    return;
  }
  server.send(200, "application/json; charset=utf-8", String("{\"ok\":true,\"message\":\"На дисплее страница ") + String(page) + "\"}");
  renderTFT();
}
void handleScanWiFi() {
  // Starting a scan is quick; loop() collects its result asynchronously.
  if (!routerModeEnabled && !setupApEnabled) {
    server.send(409, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Сначала включите роутер или точку доступа.\"}");
    return;
  }
  Serial.println(F("Wi-Fi scan requested from admin page."));
  if (!wifiScanActive && !startWiFiScan(WIFI_SCAN_WEB)) {
    server.send(500, "application/json; charset=utf-8", makeWiFiScanStatusJson());
    return;
  }
  server.send(202, "application/json; charset=utf-8", makeWiFiScanStatusJson());
}

void handleSaveWiFiDefaults() {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Некорректные настройки.\"}");
    return;
  }
  const bool oldRouterDefault = cfg.defaultRouterMode;
  const bool oldApDefault = cfg.defaultSetupAp;
  cfg.defaultRouterMode = doc["router"] | cfg.defaultRouterMode;
  cfg.defaultSetupAp = doc["ap"] | cfg.defaultSetupAp;
  if (!saveConfig()) {
    cfg.defaultRouterMode = oldRouterDefault;
    cfg.defaultSetupAp = oldApDefault;
    server.send(500, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Не удалось сохранить настройки. Предыдущие значения восстановлены.\"}");
    return;
  }
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"message\":\"Настройки запуска сохранены. Текущий режим не изменён; новые значения применятся после перезагрузки.\"}");
}

void handleWiFiMode() {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"Некорректные настройки Wi-Fi.\"}"); return; }
  routerModeEnabled = doc["router"] | routerModeEnabled;
  setupApEnabled = doc["ap"] | setupApEnabled;
  applyWiFiMode();
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"message\":\"Текущий режим применён. Постоянные настройки запуска не изменены.\"}");
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
  SavedNetwork previousNetworks[MAX_SAVED_NETWORKS];
  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; ++i) previousNetworks[i] = savedNetworks[i];
  const String previousSsid = cfg.ssid;
  const String previousPassword = cfg.password;
  if (!addOrUpdateSavedNetwork(newSsid, effectivePassword)) {
    server.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Память заполнена: сохранено 8 сетей.\"}");
    return;
  }
  cfg.ssid = newSsid;
  cfg.password = effectivePassword;
  if (!saveConfig()) {
    for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; ++i) savedNetworks[i] = previousNetworks[i];
    cfg.ssid = previousSsid;
    cfg.password = previousPassword;
    server.send(500, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Не удалось сохранить настройки. Предыдущая конфигурация восстановлена.\"}");
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


enum RemoteTextMode : uint8_t { RT_CLOSED, RT_PICKER, RT_EDIT, RT_WIFI_MENU, RT_CLOCK, RT_CLOCK_CONFIRM, RT_EMPTY_MESSAGE };

// Wire protocol shared with ESP8266_Gateway. The ESP8266 is the I2C master;
// ESP32 is the slave at 0x42. All multi-byte fields are little-endian.
static const uint8_t GATEWAY_I2C_ADDRESS = 0x42;
static const uint8_t GATEWAY_FRAME_MAGIC = 0xA7;
static const uint8_t GATEWAY_FRAME_VERSION = 2;
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
  float voltage, current, power;
  uint16_t currentCalibrationX600;
  uint16_t voltageCalibrationX600;
  uint16_t sensorPacketNumber;
  int8_t espNowRssi;          // dBm; valid when flags bit 2 is set
  uint8_t crc;
};
#pragma pack(pop)
static_assert(sizeof(GatewayPacket) == 32, "GatewayPacket size mismatch");
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
  if (received == 16 && stored == 16 && raw[0] == 0xA8) {
    energyEepromTransport.acceptResponse(raw, received);
    return;
  }
  gatewayRxLength = received;
  gatewayRxPending = true;

  // Prepare the acknowledgement in the receive callback, before the master
  // immediately requests it. Keep all heavier state updates in loop().
  bool valid = received == sizeof(GatewayPacket) || received == 31;
  if (valid) {
    GatewayPacket packet = {};
    if (received == 31) {
      // Compact 31-byte gateway frame; the final byte is its CRC.
      memcpy(&packet, raw, 30);
      packet.espNowRssi = -127;
      valid = gatewayCrc8(raw, 30) == raw[30];
    } else {
      memcpy(&packet, raw, sizeof(packet));
      valid = gatewayCrc8(raw, sizeof(packet) - 1) == packet.crc;
    }
    valid = valid && packet.magic == GATEWAY_FRAME_MAGIC &&
            packet.version == GATEWAY_FRAME_VERSION;
    gatewayAckSequence = packet.sequence;
  } else if (received >= 3) {
    gatewayAckSequence = raw[2];
  }
  gatewayAckStatus = valid ? 0 : 1;
}
static void gatewayOnRequest() {
  // ACK, RTC/OTA command and optional calibration payload.
  if (!clockCommandPending && !calibrationCommandPending && calibrationQueueTail != calibrationQueueHead) {
    calibrationCommandInFlight = calibrationQueue[calibrationQueueTail];
    calibrationQueueTail = (uint8_t)((calibrationQueueTail + 1) % (MAX_STRING_SENSORS + 1));
    calibrationCommandPending = true;
    ++calibrationCommandSequence;
  }
  uint8_t memoryType = 0, memorySequence = 0, memoryLength = 0;
  uint16_t memoryAddress = 0;
  uint8_t memoryData[8] = {};
  const bool memoryCommandPending = !clockCommandPending && !calibrationCommandPending &&
    energyEepromTransport.takeCommand(memoryType, memorySequence, memoryAddress, memoryLength, memoryData);
  uint8_t commandType = clockCommandPending ? clockCommandType :
    (calibrationCommandPending ? 3 : (memoryCommandPending ? memoryType : 0));
  uint8_t commandSequence = clockCommandPending ? clockCommandSequence :
    (calibrationCommandPending ? calibrationCommandSequence : (memoryCommandPending ? memorySequence : 0));
  uint8_t response[21] = { GATEWAY_ACK_MAGIC, gatewayAckSequence, gatewayAckStatus,
    commandType, commandSequence,
    (uint8_t)(clockCommandYear & 0xFF), (uint8_t)(clockCommandYear >> 8),
    clockCommandMonth, clockCommandDay, clockCommandHour, clockCommandMinute, clockCommandSecond,
    calibrationCommandInFlight.sensorId };
  memcpy(response + 13, &calibrationCommandInFlight.currentFactor, sizeof(float));
  memcpy(response + 17, &calibrationCommandInFlight.voltageFactor, sizeof(float));
  if (memoryCommandPending) {
    response[5] = (uint8_t)(memoryAddress & 0xFF);
    response[6] = (uint8_t)(memoryAddress >> 8);
    response[7] = memoryLength;
    memcpy(response + 8, memoryData, sizeof(memoryData));
    response[20] = gatewayCrc8(response + 3, 17);
  }
  Wire.write(response, sizeof(response));
  if (clockCommandPending) clockCommandPending = false;
  else if (calibrationCommandPending) calibrationCommandPending = false;
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
    GatewayPacket packet = {};
    bool valid = length == sizeof(packet) || length == 31;
    if (valid) {
      if (length == 31) {
        // Compact 31-byte gateway frame; its final byte is the CRC.
        memcpy(&packet, raw, 30);
        packet.espNowRssi = -127;
        valid = gatewayCrc8(raw, 30) == raw[30];
      } else {
        memcpy(&packet, raw, sizeof(packet));
        valid = gatewayCrc8(raw, sizeof(packet) - 1) == packet.crc;
      }
      valid = valid && packet.magic == GATEWAY_FRAME_MAGIC &&
              packet.version == GATEWAY_FRAME_VERSION;
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
      gatewaySensorPacketNumber = packet.sensorPacketNumber;
      if (gatewaySensorValid && packet.sensorId != 0) {
        const uint32_t sampleTimeMs = millis();
        // The energy core accepts every non-zero uint8_t sensor ID (1..255).
        energyCounter.update(packet.sensorId, packet.power, sampleTimeMs);
        energyCalendarStore.update(packet.sensorId, packet.power, sampleTimeMs,
                                   gatewayTimeValid, gatewayYear, gatewayMonth, gatewayDay);

        const uint16_t telemetryIndex = (uint16_t)packet.sensorId - 1U;
        stringSensorValid[telemetryIndex] = true;
        stringSensorLastSeenMs[telemetryIndex] = sampleTimeMs;
        stringSensorVoltage[telemetryIndex] = packet.voltage;
        stringSensorCurrent[telemetryIndex] = packet.current;
        stringSensorPower[telemetryIndex] = packet.power;

        // Calibration controls retain their existing first-ten layout for now.
        if (packet.sensorId <= MAX_STRING_SENSORS) {
          const uint8_t index = packet.sensorId - 1;
          activeCurrentCalibration[index] = packet.currentCalibrationX600 / 600.0f;
          activeVoltageCalibration[index] = packet.voltageCalibrationX600 / 600.0f;
          activeCalibrationValid[index] = packet.currentCalibrationX600 >= 1 && packet.voltageCalibrationX600 >= 1;
          // The sensor is authoritative: keep browser and IR calibration math aligned
          // with the coefficients actually applied by the live sensor.
          if (activeCalibrationValid[index]) {
            cfg.currentCalibration[index] = activeCurrentCalibration[index];
            cfg.voltageCalibration[index] = activeVoltageCalibration[index];
          }
        }
      }
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
        if (gatewaySensorValid && gatewaySensorId >= 1 && gatewaySensorId <= MAX_STRING_SENSORS) {
          const uint8_t logIndex = gatewaySensorId - 1;
          Serial.printf(" | sensor %u: %.2f V, %.3f A, %.1f W, K=%.3f KU=%.3f, packet %u\n",
                        (unsigned)gatewaySensorId, gatewayVoltage, gatewayCurrent, gatewayPower,
                        activeCurrentCalibration[logIndex], activeVoltageCalibration[logIndex],
                        (unsigned)gatewaySensorPacketNumber);
        } else if (gatewaySensorValid) {
          Serial.printf(" | sensor %u: %.2f V, %.3f A, %.1f W, packet %u\n",
                        (unsigned)gatewaySensorId, gatewayVoltage, gatewayCurrent, gatewayPower,
                        (unsigned)gatewaySensorPacketNumber);
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
static uint32_t rtEmptyMessageUntil = 0;
static uint8_t rtPick = 0;
static int8_t rtWidget = -1;
static int8_t rtElement = -1;
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
static bool rtWifiShowingNetworks = false;
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
  for(uint8_t i=0;i<p.widgetCount;i++){
    const WidgetConfig& w=p.widgets[i]; if(!w.enabled)continue;
    for(uint8_t e=0;e<w.elementCount&&e<MAX_WIDGET_ELEMENTS;e++)
      if(w.elements[e].kind==ELEMENT_TEXT)++n;
  }
  return n;
}
static int rtCount(){ return (rtTextCount() > 6 ? 6 : rtTextCount()) + 1; }
static int rtIndex(uint8_t wanted){
  if(cfg.tftPage<1||cfg.tftPage>cfg.pageCount)return -1;
  PageConfig& p=cfg.pages[cfg.tftPage-1]; uint8_t n=0;
  for(uint8_t i=0;i<p.widgetCount&&n<6;i++){
    WidgetConfig& w=p.widgets[i]; if(!w.enabled)continue;
    for(uint8_t e=0;e<w.elementCount&&e<MAX_WIDGET_ELEMENTS;e++){
      if(w.elements[e].kind!=ELEMENT_TEXT)continue;
      if(n++==wanted){rtElement=e;return i;}
    }
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
  tft.fillScreen(ST77XX_BLACK); drawCyrillicText("ТЕКСТОВЫЕ ЭЛЕМЕНТЫ",8,25,ST77XX_WHITE,true);
  drawCyrillicText("UP/DOWN выбор, OK ввод",8,48,0x07FF,false);
  PageConfig& p=cfg.pages[cfg.tftPage-1]; int y=82; uint8_t n=0;
  for(uint8_t i=0;i<p.widgetCount&&n<6;i++){
    WidgetConfig& w=p.widgets[i]; if(!w.enabled)continue;
    for(uint8_t e=0;e<w.elementCount&&e<MAX_WIDGET_ELEMENTS&&n<6;e++){
      WidgetElementConfig& element=w.elements[e]; if(element.kind!=ELEMENT_TEXT)continue;
      if(n==rtPick)tft.fillRoundRect(5,y-16,230,28,4,0x03EF);
      String item=w.label.length()?w.label+": "+element.label:element.label;
      drawCyrillicText((n==rtPick?"> ":"  ")+item.substring(0,27),10,y+2,ST77XX_WHITE,false);
      y+=30;n++;
    }
  }
  if(n==rtPick)tft.fillRoundRect(5,y-16,230,28,4,0x03EF);
  drawCyrillicText((n==rtPick?"> ":"  ")+String("Часы и дата — настроить"),10,y+2,ST77XX_WHITE,false);
  drawCyrillicText("MENU: выход",8,307,0xBDF7,false);
}
static void rtDrawEditor(){
  tft.fillScreen(ST77XX_BLACK);
  PageConfig& p=cfg.pages[cfg.tftPage-1];
  if (rtWifiPassword) drawCyrillicText("ПАРОЛЬ WI-FI",5,13,0x07FF,false);
  else if (rtWidget>=0 && rtElement>=0) {
    WidgetConfig& w=p.widgets[rtWidget];
    drawCyrillicText((w.label+": "+w.elements[rtElement].label).substring(0,30),5,13,0x07FF,false);
  }

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

  const int x0=2, cellW=23, cellH=29;
  const uint8_t keyboardRows = rtSymbolsPage ? 6 : 4;
  // Keep the keyboard anchored to the bottom edge of the 320px TFT.
  const int y0 = 320 - keyboardRows * cellH - 2;
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
  if (rtWifiShowingNetworks) {
    drawCyrillicText("Выбери сеть: UP/DOWN, OK",5,45,0xBDF7,false);
    const uint8_t first = (rtWifiPick / 5) * 5;
    for (uint8_t row = 0; row < 5 && first + row < rtWifiCount; ++row) {
      const uint8_t index = first + row;
      const int y = 77 + row * 39;
      if (index == rtWifiPick) tft.fillRoundRect(3,y-17,234,33,4,0x03EF);
      String label = rtWifiNetworks[index];
      if (label.length() > 23) label = label.substring(0,23);
      drawCyrillicText(label,8,y,ST77XX_WHITE,false);
      drawCyrillicText(rtWifiSecure[index] ? "LOCK" : "OPEN",8,y+13,0xBDF7,false);
    }
    if (message.length()) drawCyrillicText(message.substring(0,30),5,285,ST77XX_YELLOW,false);
    drawCyrillicText("SEARCH: повтор  MENU: выход",5,307,0xBDF7,false);
    return;
  }
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
  if (wifiScanActive) { rtWifiDrawMenu("Поиск уже выполняется"); return; }
  if(!routerModeEnabled && !setupApEnabled){rtWifiDrawMenu("Сначала включи Wi-Fi (1 или 2)");return;}
  WiFi.mode(setupApEnabled && routerModeEnabled ? WIFI_AP_STA : (setupApEnabled ? WIFI_AP : WIFI_STA));
  rtWifiShowingNetworks = false;
  rtWifiCount = 0;
  if (!startWiFiScan(WIFI_SCAN_REMOTE)) {
    rtWifiDrawMenu("Не удалось запустить поиск");
    return;
  }
  rtWifiDrawMenu("Идёт поиск сетей Wi-Fi...");
}

static void updateWiFiScan() {
  if (!wifiScanActive) return;
  const int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) return;

  wifiScanCount = 0;
  wifiScanFailed = count == WIFI_SCAN_FAILED;
  if (!wifiScanFailed) {
    for (int i = 0; i < count && wifiScanCount < 20; ++i) {
      String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;
      bool duplicate = false;
      for (uint8_t j = 0; j < wifiScanCount; ++j)
        if (wifiScanSsids[j] == ssid) { duplicate = true; break; }
      if (duplicate) continue;
      wifiScanSsids[wifiScanCount] = ssid;
      wifiScanRssi[wifiScanCount] = WiFi.RSSI(i);
      wifiScanSecure[wifiScanCount] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      ++wifiScanCount;
    }
  }
  WiFi.scanDelete();
  wifiScanActive = false;
  wifiScanReady = true;

  if (wifiScanOwner == WIFI_SCAN_REMOTE) {
    rtWifiCount = wifiScanCount;
    for (uint8_t i = 0; i < rtWifiCount; ++i) {
      rtWifiNetworks[i] = wifiScanSsids[i];
      rtWifiSecure[i] = wifiScanSecure[i];
    }
    rtWifiPick = 0;
    rtWifiShowingNetworks = !wifiScanFailed && rtWifiCount > 0;
    if (wifiScanFailed) rtWifiDrawMenu("Ошибка сканирования Wi-Fi");
    else if (rtWifiCount) rtWifiDrawMenu();
    else rtWifiDrawMenu("Сети не найдены");
  }
  wifiScanOwner = WIFI_SCAN_NONE;
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
  rtWifiShowingNetworks = false;
  rtWifiSelectedSSID=rtWifiNetworks[rtWifiPick];
  int saved=findSavedNetwork(rtWifiSelectedSSID);
  if(saved>=0){rtWifiConnect(rtWifiSelectedSSID,savedNetworks[saved].password);return;}
  if(!rtWifiSecure[rtWifiPick]){rtWifiConnect(rtWifiSelectedSSID,"");return;}
  rtBuffer="";rtCursor=0;rtWifiPassword=true;rtRussian=false;rtUpper=true;rtSymbolsPage=false;rtKeyRow=0;rtKeyCol=0;rtMode=RT_EDIT;rtDrawEditor();
}

static void rtOpen(){
  if(!rtCount()){
    rtMode=RT_EMPTY_MESSAGE;
    rtEmptyMessageUntil=millis()+900UL;
    tft.fillScreen(ST77XX_BLACK);
    drawCyrillicText("Нет текстовых элементов",8,35,ST77XX_WHITE,true);
    drawCyrillicText("Добавь элемент в веб-админке",8,62,0xBDF7,false);
    return;
  }
  rtMode=RT_PICKER;rtPick=0;rtDrawPicker();
}
static void rtOpenClockEditor();
static void rtEdit(){
  rtWidget=rtIndex(rtPick);if(rtWidget==-2){rtOpenClockEditor();return;}if(rtWidget<0){rtMode=RT_CLOSED;renderTFT();return;}
  rtBuffer=cfg.pages[cfg.tftPage-1].widgets[rtWidget].elements[rtElement].label;if(rtBuffer.length()>54)rtBuffer=rtBuffer.substring(0,54);
  rtCursor=rtBuffer.length();rtMode=RT_EDIT;rtKeyRow=0;rtKeyCol=0;rtUpper=true;rtRussian=true;rtSymbolsPage=false;rtDrawEditor();
}
static void rtInsert(String ch){
  const size_t maxLength = rtWifiPassword ? 63U : 54U;
  if(!ch.length()||rtBuffer.length()+ch.length()>maxLength)return;
  rtBuffer=rtBuffer.substring(0,rtCursor)+ch+rtBuffer.substring(rtCursor);rtCursor+=ch.length();rtDrawEditor();
}
static void rtSave(){
  if(rtWifiPassword){rtWifiConnect(rtWifiSelectedSSID,rtBuffer);rtWifiPassword=false;return;}
  if(rtWidget>=0&&rtElement>=0&&cfg.tftPage>=1&&cfg.tftPage<=cfg.pageCount){
    WidgetConfig& widget=cfg.pages[cfg.tftPage-1].widgets[rtWidget];
    if(rtElement<widget.elementCount){
      String value=sanitizeTitle(rtBuffer);
      widget.elements[rtElement].label=value.length()?value:"Текст";
      bool ok=saveConfig();Serial.println(ok?F("IR text element saved."):F("ERROR: text element save failed."));
    }
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
    if(k==IR_CODE_MENU||k==IR_CODE_BACK){rtWifiShowingNetworks=false;rtMode=RT_CLOSED;renderTFT();return;}
    if (rtWifiShowingNetworks) {
      if (k == IR_CODE_UP && rtWifiCount) { rtWifiPick = rtWifiPick ? rtWifiPick - 1 : rtWifiCount - 1; rtWifiDrawMenu(); return; }
      if (k == IR_CODE_DOWN && rtWifiCount) { rtWifiPick = (rtWifiPick + 1) % rtWifiCount; rtWifiDrawMenu(); return; }
      if (k == IR_CODE_OK) { rtWifiChoose(); return; }
      if (k == IR_CODE_SEARCH) { rtWifiScan(); return; }
      return;
    }
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

static void drawOtaConfirm() {
  tft.fillRect(10, 82, 220, 156, ST77XX_BLACK);
  tft.drawRoundRect(10, 82, 220, 156, 7, ST77XX_YELLOW);
  drawCyrillicText("ОБНОВЛЕНИЕ", 24, 108, ST77XX_YELLOW, true);
  drawCyrillicText("Перейти в режим", 20, 139, ST77XX_WHITE, false);
  drawCyrillicText("обновления прошивки", 20, 161, ST77XX_WHITE, false);
  drawCyrillicText("контроллера стрингов?", 20, 183, ST77XX_WHITE, false);
  drawCyrillicText("OK — ДА", 24, 218, ST77XX_GREEN, false);
  drawCyrillicText("BACK — НЕТ", 126, 218, ST77XX_RED, false);
}


bool calibrationInlineActive = false;
bool calibrationEditing = false;
uint8_t calibrationRow = 0, calibrationDigit = 0;
uint32_t calibrationTargetScaled = 0; // Desired real value, stored as thousandths.
static uint8_t calibrationStringCount() {
  if (cfg.tftPage < 1 || cfg.tftPage > cfg.pageCount) return 2;
  PageConfig& page = cfg.pages[cfg.tftPage - 1];
  uint8_t count = 0;
  for (uint8_t i = 0; i < page.widgetCount; ++i) {
    const WidgetConfig& widget = page.widgets[i];
    if (!widget.enabled) continue;
    for (uint8_t e = 0; e < widget.elementCount; ++e)
      if (widget.elements[e].kind == ELEMENT_CALIBRATION && count < 6) ++count;
  }
  return count ? count : 2;
}
static bool hasCalibrationWidgetOnCurrentPage() {
  if (cfg.tftPage < 1 || cfg.tftPage > cfg.pageCount) return false;
  PageConfig& page = cfg.pages[cfg.tftPage - 1];
  for (uint8_t i = 0; i < page.widgetCount; ++i) {
    const WidgetConfig& widget = page.widgets[i];
    if (!widget.enabled) continue;
    for (uint8_t e = 0; e < widget.elementCount; ++e)
      if (widget.elements[e].kind == ELEMENT_CALIBRATION) return true;
  }
  return false;
}
static uint8_t selectedCalibrationSensorIndex() {
  if (cfg.tftPage < 1 || cfg.tftPage > cfg.pageCount) return 0;
  PageConfig& page = cfg.pages[cfg.tftPage - 1];
  uint8_t ordinal = 0;
  for (uint8_t i = 0; i < page.widgetCount; ++i) {
    const WidgetConfig& widget = page.widgets[i];
    if (!widget.enabled) continue;
    for (uint8_t e = 0; e < widget.elementCount; ++e) {
      const WidgetElementConfig& element = widget.elements[e];
      if (element.kind != ELEMENT_CALIBRATION) continue;
      if (ordinal++ == calibrationRow / 2) return element.sourceId ? element.sourceId - 1 : 0;
    }
  }
  return 0;
}
static float& selectedCalibrationValue() {
  const uint8_t stringIndex = selectedCalibrationSensorIndex();
  return (calibrationRow % 2 == 0) ? cfg.currentCalibration[stringIndex] : cfg.voltageCalibration[stringIndex];
}
static float selectedCalibrationMeasurement() {
  const uint8_t index = selectedCalibrationSensorIndex();
  if (index >= MAX_STRING_SENSORS || !stringSensorValid[index] ||
      millis() - stringSensorLastSeenMs[index] > STRING_SENSOR_TIMEOUT_MS) return NAN;
  return (calibrationRow % 2 == 0) ? stringSensorCurrent[index] : stringSensorVoltage[index];
}
static void beginCalibrationTargetEdit() {
  const float actual = selectedCalibrationMeasurement();
  if (isfinite(actual) && actual > 0.0f) {
    // Keep targets aligned with the precision shown to the user:
    // current = 0.01 A, voltage = 0.1 V. Internally retain thousandths.
    calibrationTargetScaled = (calibrationRow % 2 == 0)
                                  ? (uint32_t)constrain((long)(roundf(actual * 100.0f) * 10.0f), 0L, 9999999L)
                                  : (uint32_t)constrain((long)(roundf(actual * 10.0f) * 100.0f), 0L, 9999999L);
  } else {
    calibrationTargetScaled = 0;
  }
  calibrationDigit = 0;
  calibrationEditing = true;
}
static void refreshCalibrationWidget() {
  if (cfg.tftPage < 1 || cfg.tftPage > cfg.pageCount) return;
  PageConfig& page = cfg.pages[cfg.tftPage - 1];
  for (uint8_t i = 0; i < page.widgetCount; ++i) {
    const WidgetConfig& widget = page.widgets[i];
    if (!widget.enabled) continue;
    for (uint8_t e = 0; e < widget.elementCount; ++e)
      if (widget.elements[e].kind == ELEMENT_CALIBRATION) {
        updateWidgetIncremental(widget, cfg.tftPage - 1, i);
        break;
      }
  }
}
static void handleInlineCalibration(uint32_t code) {
  if (code == IR_CODE_BACK || code == IR_CODE_MENU) {
    if (calibrationEditing) {
      calibrationEditing = false;
    } else {
      calibrationInlineActive = false;
    }
  } else if (!calibrationEditing) {
    const uint8_t rows = calibrationStringCount() * 2;
    if (code == IR_CODE_UP && calibrationRow > 0) --calibrationRow;
    else if (code == IR_CODE_DOWN && calibrationRow + 1 < rows) ++calibrationRow;
    else if (code == IR_CODE_OK) beginCalibrationTargetEdit();
  } else {
    const uint8_t digitCount = (calibrationRow % 2 == 0) ? 6 : 5;
    if (code == IR_CODE_LEFT) calibrationDigit = (calibrationDigit + digitCount - 1) % digitCount;
    else if (code == IR_CODE_RIGHT) calibrationDigit = (calibrationDigit + 1) % digitCount;
    else {
      int8_t enteredDigit = -1;
      if (code == IR_CODE_0) enteredDigit = 0;
      else if (code == IR_CODE_1) enteredDigit = 1;
      else if (code == IR_CODE_2) enteredDigit = 2;
      else if (code == IR_CODE_3) enteredDigit = 3;
      else if (code == IR_CODE_4) enteredDigit = 4;
      else if (code == IR_CODE_5) enteredDigit = 5;
      else if (code == IR_CODE_6) enteredDigit = 6;
      else if (code == IR_CODE_7) enteredDigit = 7;
      else if (code == IR_CODE_8) enteredDigit = 8;
      else if (code == IR_CODE_9) enteredDigit = 9;

      if (enteredDigit >= 0) {
        const uint32_t placeValues[6] = {1000000UL, 100000UL, 10000UL, 1000UL, 100UL, 10UL};
        const uint32_t place = placeValues[calibrationDigit];
        const int32_t oldDigit = (calibrationTargetScaled / place) % 10;
        int64_t scaled = (int64_t)calibrationTargetScaled +
                         ((int32_t)enteredDigit - oldDigit) * (int64_t)place;
        calibrationTargetScaled = (uint32_t)constrain((long)scaled, 0L, 9999999L);
      } else if (code == IR_CODE_UP || code == IR_CODE_DOWN) {
        const int32_t steps[6] = {1000000L, 100000L, 10000L, 1000L, 100L, 10L};
        int64_t value = (int64_t)calibrationTargetScaled +
                        (code == IR_CODE_UP ? steps[calibrationDigit] : -steps[calibrationDigit]);
        calibrationTargetScaled = (uint32_t)constrain((long)value, 0L, 9999999L);
      } else if (code == IR_CODE_OK) {
        const float actual = selectedCalibrationMeasurement();
        const float target = calibrationTargetScaled / 1000.0f;
        if (!isfinite(actual) || actual <= 0.001f) {
          Serial.println(F("ERROR: no fresh sensor reading; calibration was not changed."));
        } else if (!isfinite(target) || target <= 0.0f) {
          Serial.println(F("ERROR: desired calibration value must be greater than zero."));
        } else {
          float& factor = selectedCalibrationValue();
          const float oldFactor = factor;
          factor = constrain(roundf(oldFactor * target / actual * 1000.0f) / 1000.0f, 0.001f, 99.999f);
          if (saveConfig()) {
            queueCalibrationCommand(calibrationRow / 2);
            calibrationEditing = false;
            Serial.printf("IR calibration saved: string %u, %s, actual %.3f, target %.3f, factor %.3f\n",
                          calibrationRow / 2 + 1, (calibrationRow % 2 == 0) ? "current" : "voltage",
                          actual, target, factor);
          } else {
            factor = oldFactor;
            Serial.println(F("ERROR: calibration settings could not be saved."));
          }
        }
      }
    }
  }
  refreshCalibrationWidget();
}

static void handleIRRemote() {
  const uint32_t code = IR_RemoteUpdate();
  if (code == 0) return;
  if (otaConfirmActive) {
    if (code == IR_CODE_OK) {
      noInterrupts();
      clockCommandYear = 1; // Sensor ID 1 is carried in the low byte for OTA command type 2.
      clockCommandMonth = 0;
      clockCommandDay = 0;
      clockCommandHour = 0;
      clockCommandMinute = 0;
      clockCommandSecond = 0;
      ++clockCommandSequence;
      clockCommandType = 2;
      clockCommandPending = true;
      interrupts();
      otaConfirmActive = false;
      renderTFT();
      Serial.println(F("IR: OTA request for string controller 1 queued to gateway."));
    } else if (code == IR_CODE_BACK) {
      otaConfirmActive = false;
      renderTFT();
      Serial.println(F("IR: OTA request cancelled."));
    }
    return;
  }
  if (calibrationInlineActive) { handleInlineCalibration(code); return; }
  if (rtMode != RT_CLOSED) { rtHandle(code); return; }
  if (hasCalibrationWidgetOnCurrentPage() &&
      (code == IR_CODE_UP || code == IR_CODE_DOWN || code == IR_CODE_OK)) {
    calibrationInlineActive = true;
    handleInlineCalibration(code);
    return;
  }
  if (code == IR_CODE_OSD) {
    otaConfirmActive = true;
    drawOtaConfirm();
    return;
  }
  if (wifiStatusInlineActive) { handleInlineWifiStatus(code); return; }
  // Merely having Wi-Fi status rows on the page must not intercept horizontal
  // navigation. Enter edit mode only when the user presses UP or DOWN.
  if (hasWifiStatusWidgetOnCurrentPage() &&
      (code == IR_CODE_UP || code == IR_CODE_DOWN)) {
    wifiStatusPendingRouter = routerModeEnabled;
    wifiStatusPendingAp = setupApEnabled;
    // The first vertical key selects its corresponding row.
    wifiStatusSelectedRow = (code == IR_CODE_UP) ? 0 : 1;
    wifiStatusInlineActive = true;
    refreshWifiStatusWidget();
    return;
  }
  if (code == IR_CODE_MENU) rtOpen();
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

  energyCounter.begin(millis());
  energyEepromStore.begin(millis());
  energyCalendarStore.begin(millis());

  connectWiFiAtBoot();
  setupWebServer();
  renderTFT();
  IR_RemoteBegin();
  Serial.println(F("IR remote ready on GPIO27 (NEC)."));
}

void loop() {
  const uint32_t loopNowMs = millis();
  energyEepromTransport.poll(loopNowMs);
  energyEepromStore.poll(loopNowMs, energyCounter);
  energyCalendarStore.poll(loopNowMs);
  handleIRRemote();
  processGatewayPacket();
  if (rtMode == RT_EMPTY_MESSAGE && (int32_t)(millis() - rtEmptyMessageUntil) >= 0) {
    rtMode = RT_CLOSED;
    renderTFT();
  }
  if (rtMode == RT_CLOSED && !otaConfirmActive && millis() - lastIndicatorDraw >= 250UL) {
    lastIndicatorDraw = millis();
    drawTFTIndicators();
  }
  if (webStarted) server.handleClient();
  updateWiFiScan();

  if (restartAt != 0 && (long)(millis() - restartAt) >= 0) {
    ESP.restart();
  }

  // Common incremental renderer checks each visible widget; only changed lines are painted.
  static uint32_t lastIncrementalRefresh = 0;
  if (rtMode == RT_CLOSED && !otaConfirmActive && millis() - lastIncrementalRefresh >= 500UL &&
      cfg.tftPage >= 1 && cfg.tftPage <= cfg.pageCount) {
    lastIncrementalRefresh = millis();
    PageConfig& visiblePage = cfg.pages[cfg.tftPage - 1];
    for (uint8_t i = 0; i < visiblePage.widgetCount && i < MAX_WIDGETS_PER_PAGE; ++i)
      updateWidgetIncremental(visiblePage.widgets[i], cfg.tftPage - 1, i);
  }
}
