/*
  Solar_PV — ESP32 Admin MVP
  Target: ESP32 Dev Module, Arduino-ESP32 core 3.x
  TFT: ST7789 240x320, Adafruit_GFX + Adafruit_ST7789
  First stage only: TFT, Wi-Fi setup, browser admin, LittleFS settings.
  No sensors, RTC, OLED, or IR support in this stage.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
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
  j += "\"tftPage\":" + String(cfg.tftPage) + "}";
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
  if (server.hasArg("title")) cfg.title = sanitizeTitle(server.arg("title"));
  if (server.hasArg("powerLabel")) cfg.powerLabel = sanitizeTitle(server.arg("powerLabel"));
  if (server.hasArg("voltageLabel")) cfg.voltageLabel = sanitizeTitle(server.arg("voltageLabel"));
  if (server.hasArg("currentLabel")) cfg.currentLabel = sanitizeTitle(server.arg("currentLabel"));
  if (server.hasArg("batteryLabel")) cfg.batteryLabel = sanitizeTitle(server.arg("batteryLabel"));
  cfg.fontScale = server.arg("fontScale") == "2" ? 2 : 1;
  cfg.showPower = server.hasArg("showPower");
  cfg.showVoltage = server.hasArg("showVoltage");
  cfg.showCurrent = server.hasArg("showCurrent");
  cfg.showBattery = server.hasArg("showBattery");
  if (server.hasArg("powerW")) cfg.powerW = clampInt(server.arg("powerW").toInt(), 40, 224);
  if (server.hasArg("powerH")) cfg.powerH = clampInt(server.arg("powerH").toInt(), 36, 120);
  if (server.hasArg("voltageW")) cfg.voltageW = clampInt(server.arg("voltageW").toInt(), 40, 224);
  if (server.hasArg("voltageH")) cfg.voltageH = clampInt(server.arg("voltageH").toInt(), 36, 120);
  if (server.hasArg("currentW")) cfg.currentW = clampInt(server.arg("currentW").toInt(), 40, 224);
  if (server.hasArg("currentH")) cfg.currentH = clampInt(server.arg("currentH").toInt(), 36, 120);
  if (server.hasArg("batteryW")) cfg.batteryW = clampInt(server.arg("batteryW").toInt(), 40, 224);
  if (server.hasArg("batteryH")) cfg.batteryH = clampInt(server.arg("batteryH").toInt(), 36, 120);

  cfg.powerX = clampInt(server.arg("powerX").toInt(), 0, 240 - cfg.powerW);
  cfg.powerY = clampInt(server.arg("powerY").toInt(), 40, 290 - cfg.powerH);
  cfg.voltageX = clampInt(server.arg("voltageX").toInt(), 0, 240 - cfg.voltageW);
  cfg.voltageY = clampInt(server.arg("voltageY").toInt(), 40, 290 - cfg.voltageH);
  cfg.currentX = clampInt(server.arg("currentX").toInt(), 0, 240 - cfg.currentW);
  cfg.currentY = clampInt(server.arg("currentY").toInt(), 40, 290 - cfg.currentH);
  cfg.batteryX = clampInt(server.arg("batteryX").toInt(), 0, 240 - cfg.batteryW);
  cfg.batteryY = clampInt(server.arg("batteryY").toInt(), 40, 290 - cfg.batteryH);

  bool ok = saveConfig();
  renderTFT();
  String message = ok ? "Настройки экрана сохранены и применены." :
                        "Настройки применены, но не сохранены: файловая система недоступна.";
  server.send(ok ? 200 : 500, "application/json; charset=utf-8",
              String("{\"ok\":") + (ok ? "true" : "false") + ",\"message\":\"" + message + "\"}");
}

void handleSetPage() {
  String requested = server.arg("page");
  uint8_t page = requested == "2" ? 2 : 1;
  Serial.print(F("Admin requested TFT page: "));
  Serial.println(page);

  cfg.tftPage = page;
  if (!saveConfig()) {
    server.send(500, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Не удалось сохранить страницу в LittleFS.\"}");
    return;
  }

  server.send(200, "application/json; charset=utf-8",
              String("{\"ok\":true,\"message\":\"Выбрана страница ") + String(page) + "\"}");
  delay(20);
  renderTFT();
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
}

void loop() {
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

  // Check Wi-Fi status every 15 seconds, repaint only the footer when it changes.
  if (millis() - lastStatusDraw >= 15000UL) {
    lastStatusDraw = millis();
    drawTFTStatus();
  }
}
