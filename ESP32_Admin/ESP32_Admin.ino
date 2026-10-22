/*
  Solar_PV — ESP32 Admin MVP
  Target: ESP32 Dev Module, Arduino-ESP32 core 3.x
  TFT: ST7789 240x320, Adafruit_GFX + Adafruit_ST7789
  Current stage: TFT, Wi-Fi setup, browser admin, LittleFS settings, NEC IR remote.
  Sensors, gateway telemetry, and RTC are not integrated yet.
*/

#include <WiFi.h>
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
#include "WifiManager.h"
#include "WebAdmin.h"
#include "WebPage.h"
#include "IR_Remote.h"

static const char* AP_SSID = "SolarPV-Setup";
static const char* AP_PASS = "SolarPV8266";
Adafruit_ST7789 tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
WebServer server(80);

bool apActive = false;
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
  j += "\"ssid\":\"" + jsonEscape(cfg.ssid) + "\",";
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
    j += "{\"title\":\"" + jsonEscape(page.title) + "\",\"widgetCount\":" + String(page.widgetCount) + ",\"widgets\":[";
    for (uint8_t i = 0; i < page.widgetCount; i++) {
      if (i) j += ",";
      const WidgetConfig& w = page.widgets[i];
      j += "{\"type\":\"" + jsonEscape(w.type) + "\",\"label\":\"" + jsonEscape(w.label) + "\",\"text\":\"" + jsonEscapeText(w.text) + "\",";
      j += "\"headingSize\":" + String(w.headingSize) + ",\"contentSize\":" + String(w.contentSize) + ",\"stringCount\":" + String(w.stringCount ? w.stringCount : 2) + ",\"locked\":" + String(w.locked ? "true" : "false") + ",\"headingAlign\":" + String(w.headingAlign) + ",\"contentAlign\":" + String(w.contentAlign) + ",\"headingColor\":" + String(w.headingColor) + ",\"contentColor\":" + String(w.contentColor) + ",\"backgroundColor\":" + String(w.backgroundColor) + ",";
      j += "\"x\":" + String(w.x) + ",\"y\":" + String(w.y) + ",\"w\":" + String(w.w) + ",\"h\":" + String(w.h) + ",";
      j += "\"enabled\":" + String(w.enabled ? "true" : "false") + "}";
    }
    j += "]}";
  }
  j += "]}";
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
      w.headingSize = clampInt(source["headingSize"] | 1, 0, 4); w.contentSize = clampInt(source["contentSize"] | 2, 0, 4);
      w.stringCount = clampInt(source["stringCount"] | 2, 1, 6); w.locked = source["locked"] | false;
      w.headingAlign = clampInt(source["headingAlign"] | 0, 0, 2); w.contentAlign = clampInt(source["contentAlign"] | 0, 0, 2);
      w.headingColor = clampInt(source["headingColor"] | 0, 0, 5); w.contentColor = clampInt(source["contentColor"] | 3, 0, 5); w.backgroundColor = clampInt(source["backgroundColor"] | 0, 0, 5);
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
  // Keep the setup access point alive while scanning nearby 2.4 GHz networks.
  WiFi.mode(WIFI_AP_STA);
  if (!apActive) {
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    apActive = WiFi.softAP(AP_SSID, AP_PASS);
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

void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
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

  if (code == IR_CODE_LEFT || code == IR_CODE_BACK) {
    showTFTPage(cfg.tftPage > 1 ? cfg.tftPage - 1 : cfg.pageCount);
  } else if (code == IR_CODE_RIGHT || code == IR_CODE_FORWARD) {
    showTFTPage(cfg.tftPage < cfg.pageCount ? cfg.tftPage + 1 : 1);
  } else if (code == IR_CODE_TITLE || code == IR_CODE_BEGINNING) {
    showTFTPage(1);
  } else if (code == IR_CODE_END) {
    showTFTPage(cfg.pageCount);
  } else if (code == IR_CODE_DISPLAY) {
    showTFTPage(cfg.tftPage < cfg.pageCount ? cfg.tftPage + 1 : 1);
  } else if (code == IR_CODE_1) {
    showTFTPage(1);
  } else if (code == IR_CODE_2) {
    showTFTPage(2);
  } else if (code == IR_CODE_3) {
    showTFTPage(3);
  } else if (code == IR_CODE_4) {
    showTFTPage(4);
  } else {
    // Other buttons are reserved for future menu/widget controls.
    Serial.printf("IR remote: received code 0x%08lX (no action assigned yet)\n",
                  static_cast<unsigned long>(code));
  }
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

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  connectWiFiAtBoot();
  setupWebServer();
  renderTFT();
  IR_RemoteBegin();
  Serial.println(F("IR remote ready on GPIO27 (NEC)."));
}

void loop() {
  handleIRRemote();
  if (webStarted) server.handleClient();

  if (restartAt != 0 && (long)(millis() - restartAt) >= 0) {
    delay(100);
    ESP.restart();
  }

  if (cfg.ssid.length() > 0) {
    if (WiFi.status() == WL_CONNECTED) {
      disconnectedSince = 0;
      // Keep both router and setup AP available; do not spam Serial.
    } else {
      if (disconnectedSince == 0) disconnectedSince = millis();
      if (!apActive && millis() - disconnectedSince >= 30000UL) startAccessPoint();
    }
  }

  // Refresh live Wi-Fi widgets periodically; otherwise update only the footer.
  if (millis() - lastStatusDraw >= 15000UL) {
    lastStatusDraw = millis();
    bool hasDynamicWidget = false;
    if (cfg.tftPage >= 1 && cfg.tftPage <= cfg.pageCount) {
      PageConfig& page = cfg.pages[cfg.tftPage - 1];
      for (uint8_t i = 0; i < page.widgetCount; i++) {
        if (page.widgets[i].enabled && page.widgets[i].type == "wifiStatus") hasDynamicWidget = true;
      }
    }
    if (hasDynamicWidget) renderTFT();
    else drawTFTStatus();
  }
}
