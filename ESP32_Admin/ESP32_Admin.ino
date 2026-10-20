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











void drawCyrillicText(const String& text, int x, int baselineY, uint16_t color, bool large) {
  u8g2Fonts.setForegroundColor(color);
  u8g2Fonts.setFont(large ? u8g2_font_unifont_t_cyrillic : u8g2_font_6x13_t_cyrillic);
  u8g2Fonts.setCursor(x, baselineY);
  u8g2Fonts.print(text);
}

void drawCard(const char* label, const char* value, int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, 5, 0x29C6);
  tft.drawRoundRect(x, y, w, h, 5, 0x5B6D);
  drawCyrillicText(String(label), x + 6, y + 19, 0xBDF7, false);
  drawCyrillicText(String(value), x + 6, y + 43, 0xFEA0, cfg.fontScale == 2);
}


String getTFTStatusText() {
  if (WiFi.status() == WL_CONNECTED) {
    return "Wi-Fi " + String(WiFi.RSSI()) + " dBm " + WiFi.localIP().toString();
  } else if (apActive) {
    return "Точка настройки: " + WiFi.softAPIP().toString();
  }
  return "Подключение к Wi-Fi...";
}

void drawTFTStatus(bool force = false) {
  String status = getTFTStatusText();
  if (!force && status == lastStatusText) return;

  // Clear only the footer status area; do not flash the whole display.
  tft.fillRect(0, 296, 240, 24, ST77XX_BLACK);
  drawCyrillicText(status, 8, 313, 0xBDF7, false);
  lastStatusText = status;
}

void renderTFT() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  if (cfg.tftPage == 2) {
    drawCyrillicText("СОСТОЯНИЕ WI-FI", 8, 25, ST77XX_WHITE, true);
    drawCyrillicText("Страница 2", 8, 45, 0x07E0, false);
    if (WiFi.status() == WL_CONNECTED) {
      drawCyrillicText("Сеть: " + cfg.ssid, 8, 78, 0xBDF7, false);
      drawCyrillicText("Сигнал: " + String(WiFi.RSSI()) + " dBm", 8, 103, 0xBDF7, false);
      drawCyrillicText("IP роутера: " + WiFi.localIP().toString(), 8, 128, 0xBDF7, false);
    } else {
      drawCyrillicText("Роутер не подключён", 8, 78, 0xF800, false);
      drawCyrillicText("Сеть: " + cfg.ssid, 8, 103, 0xBDF7, false);
    }
    if (apActive) {
      drawCyrillicText("Точка настройки:", 8, 165, 0xBDF7, false);
      drawCyrillicText("SolarPV-Setup", 8, 188, 0xBDF7, false);
      drawCyrillicText("IP точки: " + WiFi.softAPIP().toString(), 8, 211, 0xBDF7, false);
    }
    tft.drawFastHLine(8, 294, 224, 0x5B6D);
    lastStatusText = "";
    drawTFTStatus(true);
    return;
  }

  drawCyrillicText(cfg.title, 8, 25, ST77XX_WHITE, true);
  drawCyrillicText("ДЕМО-ДАННЫЕ", 8, 43, 0x07E0, false);

  if (cfg.showPower) drawCard("Мощность PV", "DEMO W", cfg.powerX, cfg.powerY, 224, 58);
  if (cfg.showVoltage) drawCard("Напряжение", "DEMO V", cfg.voltageX, cfg.voltageY, 106, 58);
  if (cfg.showCurrent) drawCard("Ток", "DEMO A", cfg.currentX, cfg.currentY, 106, 58);
  if (cfg.showBattery) drawCard("Аккумулятор", "DEMO %", cfg.batteryX, cfg.batteryY, 224, 58);

  tft.drawFastHLine(8, 294, 224, 0x5B6D);
  lastStatusText = "";
  drawTFTStatus(true);
}


String makeConfigJson() {
  String j = "{";
  j += "\"title\":\"" + jsonEscape(cfg.title) + "\",";
  j += "\"fontScale\":" + String(cfg.fontScale) + ",";
  j += "\"showPower\":" + String(cfg.showPower ? "true" : "false") + ",";
  j += "\"showVoltage\":" + String(cfg.showVoltage ? "true" : "false") + ",";
  j += "\"showCurrent\":" + String(cfg.showCurrent ? "true" : "false") + ",";
  j += "\"showBattery\":" + String(cfg.showBattery ? "true" : "false") + ",";
  j += "\"powerX\":" + String(cfg.powerX) + ",\"powerY\":" + String(cfg.powerY) + ",";
  j += "\"voltageX\":" + String(cfg.voltageX) + ",\"voltageY\":" + String(cfg.voltageY) + ",";
  j += "\"currentX\":" + String(cfg.currentX) + ",\"currentY\":" + String(cfg.currentY) + ",";
  j += "\"batteryX\":" + String(cfg.batteryX) + ",\"batteryY\":" + String(cfg.batteryY) + ",";
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

void startAccessPoint() {
  if (apActive) return;
  if (cfg.ssid.length() > 0) WiFi.mode(WIFI_AP_STA);
  else WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  apActive = WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println();
  Serial.println(F("Setup access point requested."));
  Serial.print(F("SSID: ")); Serial.println(AP_SSID);
  Serial.println(F("Open http://192.168.4.1"));
  renderTFT();
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
  cfg.fontScale = server.arg("fontScale") == "2" ? 2 : 1;
  cfg.showPower = server.hasArg("showPower");
  cfg.showVoltage = server.hasArg("showVoltage");
  cfg.showCurrent = server.hasArg("showCurrent");
  cfg.showBattery = server.hasArg("showBattery");
  cfg.powerX = clampInt(server.arg("powerX").toInt(), 0, 16);
  cfg.powerY = clampInt(server.arg("powerY").toInt(), 40, 230);
  cfg.voltageX = clampInt(server.arg("voltageX").toInt(), 0, 128);
  cfg.voltageY = clampInt(server.arg("voltageY").toInt(), 40, 230);
  cfg.currentX = clampInt(server.arg("currentX").toInt(), 0, 128);
  cfg.currentY = clampInt(server.arg("currentY").toInt(), 40, 230);
  cfg.batteryX = clampInt(server.arg("batteryX").toInt(), 0, 16);
  cfg.batteryY = clampInt(server.arg("batteryY").toInt(), 40, 230);

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

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleApiConfig);
  server.on("/api/scan", HTTP_GET, handleScanWiFi);
  server.on("/api/save", HTTP_POST, handleSaveDisplay);
  server.on("/api/page", HTTP_GET, handleSetPage);
  server.on("/api/page", HTTP_POST, handleSetPage);
  server.on("/api/wifi", HTTP_POST, handleSaveWiFi);
  server.onNotFound(handleNotFound);
  server.begin();
  webStarted = true;
  Serial.println(F("Web admin started on port 80."));
}

void connectWiFiAtBoot() {
  if (cfg.ssid.length() == 0) {
    startAccessPoint();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
  Serial.print(F("Connecting to configured Wi-Fi"));
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000UL) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    disconnectedSince = 0;
    Serial.print(F("Connected. IP: "));
    Serial.println(WiFi.localIP());
    startAccessPoint();  // Keep both router and setup AP available.
  } else {
    Serial.println(F("Wi-Fi connection failed; enabling setup AP."));
    startAccessPoint();
    disconnectedSince = millis();
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println(F("Solar_PV ESP32 Admin MVP"));

  fsReady = LittleFS.begin(false);  // Never auto-format: preserve existing data on mount errors.
  if (!fsReady) Serial.println(F("ERROR: LittleFS mount failed; automatic format is disabled. Settings cannot be saved."));

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
