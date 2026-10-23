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


enum RemoteTextMode : uint8_t { RT_CLOSED, RT_PICKER, RT_EDIT };
static RemoteTextMode rtMode = RT_CLOSED;
static uint8_t rtPick = 0;
static int8_t rtWidget = -1;
static String rtBuffer;
static size_t rtCursor = 0;
static bool rtRussian = true, rtUpper = true;
static uint8_t rtKeyRow = 1, rtKeyCol = 0;
static bool rtSymbolsPage = false;

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
static int rtCount(){
  if(cfg.tftPage<1||cfg.tftPage>cfg.pageCount)return 0;
  int n=0; PageConfig& p=cfg.pages[cfg.tftPage-1];
  for(uint8_t i=0;i<p.widgetCount;i++)if(p.widgets[i].enabled&&p.widgets[i].type=="text")n++;
  return n;
}
static int rtIndex(uint8_t wanted){
  if(cfg.tftPage<1||cfg.tftPage>cfg.pageCount)return -1;
  PageConfig& p=cfg.pages[cfg.tftPage-1]; uint8_t n=0;
  for(uint8_t i=0;i<p.widgetCount;i++)if(p.widgets[i].enabled&&p.widgets[i].type=="text"){
    if(n++==wanted)return i;
  }
  return -1;
}
static String rtKeyAt(uint8_t row, uint8_t col) {
  if (row < 5) {
    if (rtSymbolsPage) {
      static const char* symbols[5] = {"0123456789","!@#$%^&*()","-_=+[]{}<>",".,;:'\\\"~","/?\\\\|%&^"};
      return String(symbols[row]).substring(col, col + 1);
    }
    if (row == 4) return String(".,!?-_:;@#").substring(col, col + 1);
    static const char* enUpper[4] = {"ABCDEFGHIJ","KLMNOPQRST","UVWXYZ    ","          "};
    static const char* enLower[4] = {"abcdefghij","klmnopqrst","uvwxyz    ","          "};
    static const char* ruUpper[4] = {"АБВГДЕЁЖЗИ","ЙКЛМНОПРСТ","УФХЦЧШЩЪЫЬ","ЭЮЯ       "};
    static const char* ruLower[4] = {"абвгдеёжзи","йклмнопрст","уфхцчшщъыь","эюя       "};
    String s;
    if (rtRussian) s = rtUpper ? ruUpper[row] : ruLower[row];
    else s = rtUpper ? enUpper[row] : enLower[row];
    size_t pos = 0;
    for (uint8_t n = 0; n < col && pos < s.length(); n++) pos += rtBytes(s, pos);
    String key = rtChar(s, pos);
    return key == " " ? String("") : key;
  }
  static const char* actions[10] = {".",",","!","?","SP","DEL","NL","RU","Aa","SAVE"};
  return actions[col];
}
static void rtDrawPicker(){
  tft.fillScreen(ST77XX_BLACK); drawCyrillicText("ТЕКСТОВЫЙ БЛОК",8,25,ST77XX_WHITE,true);
  drawCyrillicText("UP/DOWN выбор, OK ввод",8,48,0x07FF,false);
  PageConfig& p=cfg.pages[cfg.tftPage-1]; int y=82; uint8_t n=0;
  for(uint8_t i=0;i<p.widgetCount&&n<7;i++){WidgetConfig& w=p.widgets[i];if(!w.enabled||w.type!="text")continue;
    if(n==rtPick)tft.fillRoundRect(5,y-16,230,28,4,0x03EF);
    drawCyrillicText((n==rtPick?"> ":"  ")+w.label.substring(0,27),10,y+2,ST77XX_WHITE,false);y+=30;n++;
  }
  drawCyrillicText("MENU: выход",8,307,0xBDF7,false);
}
static void rtDrawEditor(){
  tft.fillScreen(ST77XX_BLACK);
  PageConfig& p=cfg.pages[cfg.tftPage-1]; WidgetConfig& w=p.widgets[rtWidget];
  drawCyrillicText(w.label.substring(0,30),5,13,0x07FF,false);
  drawCyrillicText(rtSymbolsPage ? "123 / SEARCH: ABC" : "ABC / SEARCH: 123",150,13,0x07FF,false);

  // A compact preview of the text being edited, with a caret at the insertion point.
  String view=rtBuffer.substring(0,rtCursor)+"_"+rtBuffer.substring(rtCursor);
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
  for(uint8_t row=0;row<6;row++){
    for(uint8_t col=0;col<10;col++){
      int x=x0+col*cellW, yk=y0+row*cellH;
      bool selected=(row==rtKeyRow&&col==rtKeyCol);
      if(selected)tft.fillRoundRect(x,yk,cellW-1,cellH-2,3,0x03EF);
      else tft.drawRoundRect(x,yk,cellW-1,cellH-2,3,0x4208);
      String key=rtKeyAt(row,col);
      int tw=u8g2Fonts.getUTF8Width(key.c_str());
      int tx=x+(cellW-tw)/2;
      if(tx<x+1)tx=x+1;
      drawCyrillicText(key,tx,yk+19,selected?ST77XX_WHITE:0xBDF7,false);
    }
  }
}
static void rtOpen(){
  if(!rtCount()){tft.fillScreen(ST77XX_BLACK);drawCyrillicText("Нет текстовых блоков",8,35,ST77XX_WHITE,true);drawCyrillicText("Добавь блок в веб-админке",8,62,0xBDF7,false);delay(900);renderTFT();return;}
  rtMode=RT_PICKER;rtPick=0;rtDrawPicker();
}
static void rtEdit(){
  rtWidget=rtIndex(rtPick);if(rtWidget<0){rtMode=RT_CLOSED;renderTFT();return;}
  rtBuffer=cfg.pages[cfg.tftPage-1].widgets[rtWidget].text;if(rtBuffer.length()>240)rtBuffer=rtBuffer.substring(0,240);
  rtCursor=rtBuffer.length();rtMode=RT_EDIT;rtKeyRow=0;rtKeyCol=0;rtUpper=true;rtRussian=true;rtSymbolsPage=false;rtDrawEditor();
}
static void rtInsert(String ch){
  if(!ch.length()||rtBuffer.length()+ch.length()>240)return;
  rtBuffer=rtBuffer.substring(0,rtCursor)+ch+rtBuffer.substring(rtCursor);rtCursor+=ch.length();rtDrawEditor();
}
static void rtSave(){
  if(rtWidget>=0&&cfg.tftPage>=1&&cfg.tftPage<=cfg.pageCount){
    cfg.pages[cfg.tftPage-1].widgets[rtWidget].text=rtBuffer;
    bool ok=saveConfig();Serial.println(ok?F("IR text block saved."):F("ERROR: text block save failed."));
  }
  rtMode=RT_CLOSED;renderTFT();
}
static void rtHandle(uint32_t k){
  if(rtMode==RT_PICKER){
    uint8_t n=rtCount();if(!n){rtMode=RT_CLOSED;renderTFT();return;}
    if(k==IR_CODE_UP){rtPick=rtPick?rtPick-1:n-1;rtDrawPicker();}
    else if(k==IR_CODE_DOWN){rtPick=(rtPick+1)%n;rtDrawPicker();}
    else if(k==IR_CODE_OK)rtEdit();
    else if(k==IR_CODE_MENU||k==IR_CODE_BACK){rtMode=RT_CLOSED;renderTFT();}
    return;
  }
  if(rtMode!=RT_EDIT)return;
  if(k==IR_CODE_MENU){rtMode=RT_CLOSED;renderTFT();return;}
  if(k==IR_CODE_SEARCH){rtSymbolsPage=!rtSymbolsPage;rtKeyRow=0;rtKeyCol=0;rtDrawEditor();return;}
  if(k==IR_CODE_0){rtInsert("0");return;} if(k==IR_CODE_1){rtInsert("1");return;} if(k==IR_CODE_2){rtInsert("2");return;} if(k==IR_CODE_3){rtInsert("3");return;} if(k==IR_CODE_4){rtInsert("4");return;} if(k==IR_CODE_5){rtInsert("5");return;} if(k==IR_CODE_6){rtInsert("6");return;} if(k==IR_CODE_7){rtInsert("7");return;} if(k==IR_CODE_8){rtInsert("8");return;} if(k==IR_CODE_9){rtInsert("9");return;}
  if(k==IR_CODE_UP){rtKeyRow=rtKeyRow?rtKeyRow-1:5;rtDrawEditor();return;}
  if(k==IR_CODE_DOWN){rtKeyRow=(rtKeyRow+1)%6;rtDrawEditor();return;}
  if(k==IR_CODE_LEFT){rtKeyCol=rtKeyCol?rtKeyCol-1:9;rtDrawEditor();return;}
  if(k==IR_CODE_RIGHT){rtKeyCol=(rtKeyCol+1)%10;rtDrawEditor();return;}
  if(k==IR_CODE_OK){
    String key=rtKeyAt(rtKeyRow,rtKeyCol);
    if(key=="SP")rtInsert(" ");
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
  }
}
