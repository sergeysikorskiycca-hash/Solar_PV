#include "ConfigStore.h"
#include <LittleFS.h>

AppConfig cfg;
SavedNetwork savedNetworks[MAX_SAVED_NETWORKS];
bool fsReady = false;

String sanitizeTitle(String s) {
  s.trim();
  String out;
  out.reserve(54);
  // Preserve complete UTF-8 sequences for Russian and Latin titles.
  for (size_t i = 0; i < s.length() && out.length() < 54; i++) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x80) {
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' ||
          c == '.' || c == ':' || c == '/') out += (char)c;
      continue;
    }
    uint8_t count = (c & 0xE0) == 0xC0 ? 2 :
                    ((c & 0xF0) == 0xE0 ? 3 :
                    ((c & 0xF8) == 0xF0 ? 4 : 0));
    if (count == 0 || i + count > s.length() || out.length() + count > 54) continue;
    bool valid = true;
    for (uint8_t j = 1; j < count; j++) {
      if (((uint8_t)s[i + j] & 0xC0) != 0x80) valid = false;
    }
    if (valid) {
      out += s.substring(i, i + count);
      i += count - 1;
    }
  }
  if (out.length() == 0) out = "SOLAR PV";
  return out;
}

int clampInt(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

void setDefaults() {
  cfg.defaultRouterMode = false;
  cfg.defaultSetupAp = true;
  cfg.ssid = "";
  cfg.password = "";
  cfg.title = "SOLAR PV";
  setDefaultUiTexts(cfg.uiTexts);
  cfg.powerLabel = "Мощность PV"; cfg.voltageLabel = "Напряжение";
  cfg.currentLabel = "Ток"; cfg.batteryLabel = "Аккумулятор";
  cfg.showPower = true;
  cfg.showVoltage = true;
  cfg.showCurrent = true;
  cfg.showBattery = true;
  cfg.fontScale = 1;
  cfg.powerX = 8; cfg.powerY = 55; cfg.powerW = 224; cfg.powerH = 58;
  cfg.voltageX = 8; cfg.voltageY = 125; cfg.voltageW = 106; cfg.voltageH = 58;
  cfg.currentX = 125; cfg.currentY = 125; cfg.currentW = 106; cfg.currentH = 58;
  cfg.batteryX = 8; cfg.batteryY = 205; cfg.batteryW = 224; cfg.batteryH = 58;
  cfg.tftPage = 1;
  for (uint8_t i = 0; i < 6; ++i) { cfg.currentCalibration[i] = 1.0f; cfg.voltageCalibration[i] = 1.0f; }
  for (uint8_t p = 0; p < MAX_PAGES; p++) {
    for (uint8_t i = 0; i < MAX_WIDGETS_PER_PAGE; i++) {
      WidgetConfig& w = cfg.pages[p].widgets[i];
      w.headingSize = 0; w.contentSize = 2; w.stringCount = 2; w.elementCount = 0; w.locked = false; w.showHeading = true; w.showBorder = true; w.headingAlign = 0; w.contentAlign = 0;
      w.headingColor = 0; w.contentColor = 3; w.backgroundColor = 0;
    }
  }
  initializeDefaultPages();
}

bool isKnownWidgetType(const String& type) {
  return type == "power" || type == "voltage" || type == "current" ||
         type == "battery" || type == "wifiStatus" || type == "wifiControl" ||
         type == "text" || type == "custom" || type == "strings" || type == "stringsVoltage" ||
         type == "stringsCurrent" || type == "stringsPower" || type == "clock" || type == "calibration";
}

static String encodeWidgetText(const String& input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c != '\r' && ((uint8_t)c >= 0x20 || c == '\t')) out += c;
  }
  return out;
}

static String decodeWidgetText(const String& input) {
  String out;
  out.reserve(input.length());
  for (size_t i = 0; i < input.length(); i++) {
    if (input[i] == '\\' && i + 1 < input.length()) {
      char next = input[++i];
      if (next == 'n') out += '\n';
      else if (next == 't') out += '\t';
      else out += next;
    } else out += input[i];
  }
  return out;
}

void initializeDefaultPages() {
  cfg.pageCount = 2;
  cfg.pages[0].title = "Показатели";
  cfg.pages[0].showTitle = true;
  cfg.pages[0].widgetCount = 4;
  const char* types[4] = {"custom", "custom", "custom", "custom"};
  String labels[4] = {"Блок 1", "Блок 2", "Блок 3", "Блок 4"};
  int xs[4] = {cfg.powerX, cfg.voltageX, cfg.currentX, cfg.batteryX};
  int ys[4] = {cfg.powerY, cfg.voltageY, cfg.currentY, cfg.batteryY};
  int ws[4] = {cfg.powerW, cfg.voltageW, cfg.currentW, cfg.batteryW};
  int hs[4] = {cfg.powerH, cfg.voltageH, cfg.currentH, cfg.batteryH};
  bool enabled[4] = {true, true, true, true};
  for (uint8_t i = 0; i < 4; i++) {
    WidgetConfig& w = cfg.pages[0].widgets[i];
    w.type = types[i]; w.label = labels[i]; w.text = ""; w.headingSize = 1; w.contentSize = 2; w.stringCount = 2; w.elementCount = 0; w.locked = false; w.showHeading = true; w.headingAlign = 0; w.contentAlign = 0; w.headingColor = 0; w.contentColor = 3; w.backgroundColor = 0; w.x = xs[i]; w.y = ys[i];
    w.w = ws[i]; w.h = hs[i]; w.enabled = enabled[i];
  }
  cfg.pages[1].title = "Wi-Fi";
  cfg.pages[1].showTitle = true;
  cfg.pages[1].widgetCount = 1;
  WidgetConfig& wifi = cfg.pages[1].widgets[0];
  wifi.type = "wifiStatus"; wifi.label = "Состояние Wi-Fi"; wifi.text = ""; wifi.headingSize = 1; wifi.showHeading = true; wifi.contentSize = 1; wifi.stringCount = 2; wifi.elementCount = 0; wifi.locked = false; wifi.headingAlign = 0; wifi.contentAlign = 0; wifi.headingColor = 0; wifi.contentColor = 0; wifi.backgroundColor = 0;
  wifi.x = 8; wifi.y = 55; wifi.w = 224; wifi.h = 220; wifi.enabled = true;
  for (uint8_t p = 2; p < MAX_PAGES; p++) { cfg.pages[p].title = ""; cfg.pages[p].showTitle = true; cfg.pages[p].widgetCount = 0; }
}

bool loadPageKey(const String& key, const String& value) {
  if (key == "pageCount") { cfg.pageCount = clampInt(value.toInt(), 1, MAX_PAGES); return true; }
  if (key.startsWith("page")) {
    int end = 4; while (end < key.length() && isDigit(key[end])) end++;
    if (end == 4) return false;
    int p = key.substring(4, end).toInt(); if (p < 0 || p >= MAX_PAGES) return true;
    String suffix = key.substring(end);
    if (suffix == "Title") cfg.pages[p].title = sanitizeTitle(value);
    else if (suffix == "Count") cfg.pages[p].widgetCount = clampInt(value.toInt(), 0, MAX_WIDGETS_PER_PAGE);
    else if (suffix == "ShowTitle") cfg.pages[p].showTitle = value == "1";
    else return false;
    return true;
  }
  if (key.length() < 5 || key[0] != 'p' || !isDigit(key[1])) return false;
  int p = key.substring(1, 2).toInt(); int wPos = key.indexOf('w', 2);
  if (wPos < 0 || p >= MAX_PAGES) return false;
  int wiEnd = wPos + 1; while (wiEnd < key.length() && isDigit(key[wiEnd])) wiEnd++;
  int wi = key.substring(wPos + 1, wiEnd).toInt();
  if (wi < 0 || wi >= MAX_WIDGETS_PER_PAGE) return true;
  String suffix = key.substring(wiEnd); WidgetConfig& w = cfg.pages[p].widgets[wi];
  if (suffix == "ElementCount") w.elementCount = clampInt(value.toInt(), 0, MAX_WIDGET_ELEMENTS);
  else if (suffix.startsWith("Element")) {
    int elementEnd = 7;
    while (elementEnd < suffix.length() && isDigit(suffix[elementEnd])) elementEnd++;
    int elementIndex = suffix.substring(7, elementEnd).toInt();
    if (elementIndex < 0 || elementIndex >= MAX_WIDGET_ELEMENTS || elementEnd == 7) return true;
    String elementField = suffix.substring(elementEnd);
    WidgetElementConfig& element = w.elements[elementIndex];
    if (elementField == "Label") element.label = sanitizeTitle(value);
    else if (elementField == "Kind") element.kind = clampInt(value.toInt(), ELEMENT_SENSOR, ELEMENT_TEXT);
    else if (elementField == "Source") element.sourceId = clampInt(value.toInt(), 0, 7);
    else if (elementField == "Metric") element.metric = clampInt(value.toInt(), 0, 5);
    else if (elementField == "Size") element.fontSize = clampInt(value.toInt(), 0, 5);
    else if (elementField == "Color") element.color = clampInt(value.toInt(), 0, 5);
    else return false;
  }
  else if (suffix == "Type") { if (isKnownWidgetType(value)) w.type = value; }
  else if (suffix == "Label") w.label = sanitizeTitle(value);
  else if (suffix == "Text") w.text = decodeWidgetText(value);
  else if (suffix == "HeadingSize") w.headingSize = clampInt(value.toInt(), 0, 5);
  else if (suffix == "ContentSize") w.contentSize = clampInt(value.toInt(), 0, 5);
  else if (suffix == "StringCount") w.stringCount = clampInt(value.toInt(), 1, 6);
  else if (suffix == "Locked") w.locked = value == "1";
  else if (suffix == "ShowHeading") w.showHeading = value == "1";
  else if (suffix == "ShowBorder") w.showBorder = value == "1";
  else if (suffix == "HeadingAlign") w.headingAlign = clampInt(value.toInt(), 0, 2);
  else if (suffix == "ContentAlign") w.contentAlign = clampInt(value.toInt(), 0, 2);
  else if (suffix == "HeadingColor") w.headingColor = clampInt(value.toInt(), 0, 5);
  else if (suffix == "ContentColor") w.contentColor = clampInt(value.toInt(), 0, 5);
  else if (suffix == "BackgroundColor") w.backgroundColor = clampInt(value.toInt(), 0, 6);
  else if (suffix == "X") w.x = clampInt(value.toInt(), 0, 239);
  else if (suffix == "Y") w.y = clampInt(value.toInt(), 40, 289);
  else if (suffix == "W") w.w = clampInt(value.toInt(), 40, 240);
  else if (suffix == "H") w.h = clampInt(value.toInt(), 36, 250);
  else if (suffix == "Enabled") w.enabled = value == "1";
  else return false;
  return true;
}
void loadConfig() {
  setDefaults();
  if (!fsReady) return;
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return;
  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; i++) { savedNetworks[i].ssid = ""; savedNetworks[i].password = ""; }
  bool genericPagesLoaded = false;
  bool fontScaleVersion2 = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq < 1) continue;
    String key = line.substring(0, eq);
    String value = line.substring(eq + 1);
    if (key == "defaultRouterMode") cfg.defaultRouterMode = value == "1";
    else if (key == "defaultSetupAp") cfg.defaultSetupAp = value == "1";
    else if (key == "ssid") cfg.ssid = value;
    else if (key == "password") cfg.password = value;
    else if (key == "title") cfg.title = sanitizeTitle(value);
    else if (key == "fontScaleVersion") fontScaleVersion2 = value.toInt() >= 2;
    else if (key == "uiStringPrefix") cfg.uiTexts.stringPrefix = sanitizeUiText(value,"Стринг");
    else if (key == "uiCurrentLabel") cfg.uiTexts.currentLabel = sanitizeUiText(value,"Ток");
    else if (key == "uiVoltageLabel") cfg.uiTexts.voltageLabel = sanitizeUiText(value,"Напр.");
    else if (key == "uiRouterLabel") cfg.uiTexts.routerLabel = sanitizeUiText(value,"Роутер");
    else if (key == "uiApLabel") cfg.uiTexts.apLabel = sanitizeUiText(value,"Точка");
    else if (key == "uiNoRouterLabel") cfg.uiTexts.noRouterLabel = sanitizeUiText(value,"Роутер не подключён");
    else if (key == "uiNetworkLabel") cfg.uiTexts.networkLabel = sanitizeUiText(value,"Сеть");
    else if (key == "uiSignalLabel") cfg.uiTexts.signalLabel = sanitizeUiText(value,"Сигнал");
    else if (key == "uiApSetupLabel") cfg.uiTexts.apSetupLabel = sanitizeUiText(value,"Точка настройки:");
    else if (key == "uiSelectHint") cfg.uiTexts.selectHint = sanitizeUiText(value,"ВВЕРХ/ВНИЗ: выбор");
    else if (key == "uiChangeHint") cfg.uiTexts.changeHint = sanitizeUiText(value,"OK: изменить (5 сек.)");
    else if (key == "uiApplyLabel") cfg.uiTexts.applyLabel = sanitizeUiText(value,"Применение через");
    else if (key == "powerLabel") cfg.powerLabel = sanitizeTitle(value);
    else if (key == "voltageLabel") cfg.voltageLabel = sanitizeTitle(value);
    else if (key == "currentLabel") cfg.currentLabel = sanitizeTitle(value);
    else if (key == "batteryLabel") cfg.batteryLabel = sanitizeTitle(value);
    else if (key == "showPower") cfg.showPower = value == "1";
    else if (key == "showVoltage") cfg.showVoltage = value == "1";
    else if (key == "showCurrent") cfg.showCurrent = value == "1";
    else if (key == "showBattery") cfg.showBattery = value == "1";
    else if (key == "fontScale") cfg.fontScale = value == "2" ? 2 : 1;
    else if (key == "powerX") cfg.powerX = clampInt(value.toInt(), 0, 16);
    else if (key == "powerY") cfg.powerY = clampInt(value.toInt(), 40, 230);
    else if (key == "voltageX") cfg.voltageX = clampInt(value.toInt(), 0, 128);
    else if (key == "voltageY") cfg.voltageY = clampInt(value.toInt(), 40, 240);
    else if (key == "currentX") cfg.currentX = clampInt(value.toInt(), 0, 128);
    else if (key == "currentY") cfg.currentY = clampInt(value.toInt(), 40, 240);
    else if (key == "batteryX") cfg.batteryX = clampInt(value.toInt(), 0, 16);
    else if (key == "batteryY") cfg.batteryY = clampInt(value.toInt(), 40, 240);
    else if (key == "powerW") cfg.powerW = clampInt(value.toInt(), 40, 224);
    else if (key == "powerH") cfg.powerH = clampInt(value.toInt(), 36, 120);
    else if (key == "voltageW") cfg.voltageW = clampInt(value.toInt(), 40, 224);
    else if (key == "voltageH") cfg.voltageH = clampInt(value.toInt(), 36, 120);
    else if (key == "currentW") cfg.currentW = clampInt(value.toInt(), 40, 224);
    else if (key == "currentH") cfg.currentH = clampInt(value.toInt(), 36, 120);
    else if (key == "batteryW") cfg.batteryW = clampInt(value.toInt(), 40, 224);
    else if (key == "batteryH") cfg.batteryH = clampInt(value.toInt(), 36, 120);
    else if (key == "tftPage") cfg.tftPage = clampInt(value.toInt(), 1, MAX_PAGES);
    else if (key.startsWith("currentCalibration") && key.length() == 19) { int index = key.substring(18).toInt(); if (index >= 0 && index < 6) cfg.currentCalibration[index] = constrain(value.toFloat(), 0.001f, 99.999f); }
    else if (key.startsWith("voltageCalibration") && key.length() == 19) { int index = key.substring(18).toInt(); if (index >= 0 && index < 6) cfg.voltageCalibration[index] = constrain(value.toFloat(), 0.001f, 99.999f); }
    else if (key == "pageCount" || key.startsWith("page") || (key.length() > 2 && key[0] == 'p' && isDigit(key[1]))) { if (loadPageKey(key, value)) genericPagesLoaded = true; }
    else if (key.startsWith("wifi") && key.endsWith("_ssid")) { int i = key.substring(4, key.length() - 5).toInt(); if (i >= 0 && i < MAX_SAVED_NETWORKS) savedNetworks[i].ssid = value; }
    else if (key.startsWith("wifi") && key.endsWith("_password")) { int i = key.substring(4, key.length() - 9).toInt(); if (i >= 0 && i < MAX_SAVED_NETWORKS) savedNetworks[i].password = value; }
  }
  f.close();
  if (!genericPagesLoaded) initializeDefaultPages();
  // Version 1 used: 0=13px, 1=13px, 2=16px, 3=15px, 4=20px, 5=24px.
  // Version 2 removes the duplicate 13px option and inserts a 27px Cyrillic size.
  // Keep existing user choices as close as possible when migrating old LittleFS settings.
  if (genericPagesLoaded && !fontScaleVersion2) {
    for (uint8_t p = 0; p < MAX_PAGES; ++p) {
      for (uint8_t i = 0; i < MAX_WIDGETS_PER_PAGE; ++i) {
        WidgetConfig& w = cfg.pages[p].widgets[i];
        auto migrateFontSize = [](uint8_t oldSize) -> uint8_t {
          switch (oldSize) {
            case 0: case 1: return 0;
            case 2: return 2;
            case 3: return 1;
            case 4: return 3;
            default: return 4; // old 24px "Сверхкрупный"
          }
        };
        w.headingSize = migrateFontSize(w.headingSize);
        w.contentSize = migrateFontSize(w.contentSize);
      }
    }
  }
  cfg.pageCount = clampInt(cfg.pageCount, 1, MAX_PAGES);
  cfg.tftPage = clampInt(cfg.tftPage, 1, cfg.pageCount);
  for (uint8_t p = 0; p < cfg.pageCount; p++) {
    cfg.pages[p].widgetCount = clampInt(cfg.pages[p].widgetCount, 0, MAX_WIDGETS_PER_PAGE);
    for (uint8_t i = 0; i < cfg.pages[p].widgetCount; i++) {
      WidgetConfig& w = cfg.pages[p].widgets[i];
      if (!isKnownWidgetType(w.type)) w.type = "power";
      w.headingSize = clampInt(w.headingSize, 0, 5); w.contentSize = clampInt(w.contentSize, 0, 5);
      w.stringCount = clampInt(w.stringCount ? w.stringCount : 2, 1, 6);
      // Convert legacy fixed-type string monitors into editable universal rows.
      if (w.type == "strings" || w.type == "stringsVoltage" || w.type == "stringsCurrent" || w.type == "stringsPower") {
        const String legacyType = w.type;
        if (w.elementCount == 0) {
          w.elementCount = w.stringCount;
          for (uint8_t e = 0; e < w.elementCount; ++e) {
            WidgetElementConfig& element = w.elements[e];
            element.label = cfg.uiTexts.stringPrefix + " " + String(e + 1);
            element.kind = ELEMENT_SENSOR;
            element.sourceId = e + 1;
            element.metric = legacyType == "stringsCurrent" ? 1 : (legacyType == "stringsPower" ? 2 : 0);
            element.fontSize = w.contentSize;
            element.color = w.contentColor;
          }
        }
        w.type = "custom";
      }
      // Convert fixed-purpose blocks into elements of the universal block.
      if (w.type != "custom" && w.type != "strings" && w.type != "stringsVoltage" &&
          w.type != "stringsCurrent" && w.type != "stringsPower" && w.elementCount == 0) {
        const String legacyType = w.type;
        if (legacyType == "clock") {
          w.elementCount = 2;
          w.elements[0].label = "Время"; w.elements[0].kind = ELEMENT_CLOCK; w.elements[0].metric = 0;
          w.elements[1].label = "Дата"; w.elements[1].kind = ELEMENT_CLOCK; w.elements[1].metric = 1;
        } else if (legacyType == "wifiStatus" || legacyType == "wifiControl") {
          w.elementCount = 1;
          w.elements[0].label = legacyType == "wifiStatus" ? "Состояние Wi-Fi" : "Управление Wi-Fi";
          w.elements[0].kind = legacyType == "wifiStatus" ? ELEMENT_WIFI_STATUS : ELEMENT_WIFI_CONTROL;
          w.elements[0].metric = legacyType == "wifiControl" ? 2 : 0;
        } else if (legacyType == "calibration") {
          w.elementCount = clampInt(w.stringCount, 2, 6);
          for (uint8_t e = 0; e < w.elementCount; ++e) {
            w.elements[e].label = cfg.uiTexts.stringPrefix + " " + String(e + 1);
            w.elements[e].kind = ELEMENT_CALIBRATION; w.elements[e].sourceId = e + 1; w.elements[e].metric = 2;
          }
        } else if (legacyType == "text") {
          w.elementCount = 1; w.elements[0].label = w.text; w.elements[0].kind = ELEMENT_TEXT;
        } else if (legacyType == "power" || legacyType == "voltage" || legacyType == "current" || legacyType == "battery") {
          w.elementCount = 1; w.elements[0].kind = ELEMENT_SENSOR;
          w.elements[0].label = legacyType == "power" ? "Мощность PV" : (legacyType == "voltage" ? "Напряжение" : (legacyType == "current" ? "Ток" : "Аккумулятор"));
          w.elements[0].sourceId = legacyType == "power" ? 7 : 0;
          w.elements[0].metric = legacyType == "current" ? 1 : (legacyType == "power" ? 2 : (legacyType == "battery" ? 3 : 0));
        }
        w.type = "custom";
      }
      w.elementCount = clampInt(w.elementCount, 0, MAX_WIDGET_ELEMENTS);
      for (uint8_t e = 0; e < w.elementCount; ++e) {
        WidgetElementConfig& element = w.elements[e];
        element.kind = clampInt(element.kind, ELEMENT_SENSOR, ELEMENT_TEXT);
        element.sourceId = clampInt(element.sourceId, 0, 7);
        element.metric = clampInt(element.metric, 0, 5);
        element.fontSize = clampInt(element.fontSize, 0, 5);
        element.color = clampInt(element.color, 0, 5);
        if (!element.label.length()) element.label = "Элемент " + String(e + 1);
      }
      w.headingAlign = clampInt(w.headingAlign, 0, 2); w.contentAlign = clampInt(w.contentAlign, 0, 2);
      w.headingColor = clampInt(w.headingColor, 0, 5); w.contentColor = clampInt(w.contentColor, 0, 5); w.backgroundColor = clampInt(w.backgroundColor, 0, 6);
      w.w = clampInt(w.w, 40, 240); w.h = clampInt(w.h, 36, 250);
      w.x = clampInt(w.x, 0, 240 - w.w); w.y = clampInt(w.y, 40, 290 - w.h);
    }
  }
  // Migrate the currently configured network from the older single-network config.
  if (cfg.ssid.length() && findSavedNetwork(cfg.ssid) < 0) addOrUpdateSavedNetwork(cfg.ssid, cfg.password);
}

int findSavedNetwork(const String& ssid) {
  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; i++) if (ssid.length() && savedNetworks[i].ssid == ssid) return i;
  return -1;
}

bool addOrUpdateSavedNetwork(const String& ssid, const String& password) {
  int index = findSavedNetwork(ssid);
  if (index < 0) for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; i++) if (!savedNetworks[i].ssid.length()) { index = i; break; }
  if (index < 0) return false;
  savedNetworks[index].ssid = ssid; savedNetworks[index].password = password;
  return true;
}

bool saveConfig() {
  if (!fsReady) return false;
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) return false;
  f.println(String("defaultRouterMode=") + (cfg.defaultRouterMode ? "1" : "0"));
  f.println(String("defaultSetupAp=") + (cfg.defaultSetupAp ? "1" : "0"));
  f.println("ssid=" + cfg.ssid);
  f.println("password=" + cfg.password);
  f.println("title=" + sanitizeTitle(cfg.title));
  f.println("fontScaleVersion=2");
  f.println("uiStringPrefix=" + sanitizeUiText(cfg.uiTexts.stringPrefix,"Стринг"));
  f.println("uiCurrentLabel=" + sanitizeUiText(cfg.uiTexts.currentLabel,"Ток"));
  f.println("uiVoltageLabel=" + sanitizeUiText(cfg.uiTexts.voltageLabel,"Напр."));
  f.println("uiRouterLabel=" + sanitizeUiText(cfg.uiTexts.routerLabel,"Роутер"));
  f.println("uiApLabel=" + sanitizeUiText(cfg.uiTexts.apLabel,"Точка"));
  f.println("uiNoRouterLabel=" + sanitizeUiText(cfg.uiTexts.noRouterLabel,"Роутер не подключён"));
  f.println("uiNetworkLabel=" + sanitizeUiText(cfg.uiTexts.networkLabel,"Сеть"));
  f.println("uiSignalLabel=" + sanitizeUiText(cfg.uiTexts.signalLabel,"Сигнал"));
  f.println("uiApSetupLabel=" + sanitizeUiText(cfg.uiTexts.apSetupLabel,"Точка настройки:"));
  f.println("uiSelectHint=" + sanitizeUiText(cfg.uiTexts.selectHint,"ВВЕРХ/ВНИЗ: выбор"));
  f.println("uiChangeHint=" + sanitizeUiText(cfg.uiTexts.changeHint,"OK: изменить (5 сек.)"));
  f.println("uiApplyLabel=" + sanitizeUiText(cfg.uiTexts.applyLabel,"Применение через"));
  f.println("powerLabel=" + sanitizeTitle(cfg.powerLabel));
  f.println("voltageLabel=" + sanitizeTitle(cfg.voltageLabel));
  f.println("currentLabel=" + sanitizeTitle(cfg.currentLabel));
  f.println("batteryLabel=" + sanitizeTitle(cfg.batteryLabel));
  f.println(String("showPower=") + (cfg.showPower ? "1" : "0"));
  f.println(String("showVoltage=") + (cfg.showVoltage ? "1" : "0"));
  f.println(String("showCurrent=") + (cfg.showCurrent ? "1" : "0"));
  f.println(String("showBattery=") + (cfg.showBattery ? "1" : "0"));
  f.println(String("fontScale=") + String(cfg.fontScale));
  f.println("powerX=" + String(cfg.powerX));
  f.println("powerY=" + String(cfg.powerY));
  f.println("voltageX=" + String(cfg.voltageX));
  f.println("voltageY=" + String(cfg.voltageY));
  f.println("currentX=" + String(cfg.currentX));
  f.println("currentY=" + String(cfg.currentY));
  f.println("batteryX=" + String(cfg.batteryX));
  f.println("batteryY=" + String(cfg.batteryY));
  f.println("powerW=" + String(cfg.powerW)); f.println("powerH=" + String(cfg.powerH));
  f.println("voltageW=" + String(cfg.voltageW)); f.println("voltageH=" + String(cfg.voltageH));
  f.println("currentW=" + String(cfg.currentW)); f.println("currentH=" + String(cfg.currentH));
  f.println("batteryW=" + String(cfg.batteryW)); f.println("batteryH=" + String(cfg.batteryH));
  f.println("tftPage=" + String(cfg.tftPage));
  for (uint8_t i = 0; i < 6; ++i) {
    f.println("currentCalibration" + String(i) + "=" + String(cfg.currentCalibration[i], 3));
    f.println("voltageCalibration" + String(i) + "=" + String(cfg.voltageCalibration[i], 3));
  }
  f.println("pageCount=" + String(cfg.pageCount));
  for (uint8_t p = 0; p < MAX_PAGES; p++) {
    f.println("page" + String(p) + "Title=" + sanitizeTitle(cfg.pages[p].title));
    f.println("page" + String(p) + "ShowTitle=" + String(cfg.pages[p].showTitle ? 1 : 0));
    f.println("page" + String(p) + "Count=" + String(cfg.pages[p].widgetCount));
    for (uint8_t i = 0; i < MAX_WIDGETS_PER_PAGE; i++) {
      const WidgetConfig& w = cfg.pages[p].widgets[i];
      String prefix = "p" + String(p) + "w" + String(i);
      f.println(prefix + "Type=" + w.type);
      f.println(prefix + "Label=" + sanitizeTitle(w.label));
      f.println(prefix + "Text=" + encodeWidgetText(w.text));
      f.println(prefix + "HeadingSize=" + String(w.headingSize)); f.println(prefix + "ContentSize=" + String(w.contentSize));
      f.println(prefix + "StringCount=" + String(w.stringCount ? w.stringCount : 2));
      f.println(prefix + "ElementCount=" + String(w.elementCount));
      for (uint8_t e = 0; e < w.elementCount && e < MAX_WIDGET_ELEMENTS; ++e) {
        const WidgetElementConfig& element = w.elements[e];
        String elementPrefix = prefix + "Element" + String(e);
        f.println(elementPrefix + "Label=" + sanitizeTitle(element.label));
        f.println(elementPrefix + "Kind=" + String(element.kind));
        f.println(elementPrefix + "Source=" + String(element.sourceId));
        f.println(elementPrefix + "Metric=" + String(element.metric));
        f.println(elementPrefix + "Size=" + String(element.fontSize));
        f.println(elementPrefix + "Color=" + String(element.color));
      }
      f.println(prefix + "Locked=" + String(w.locked ? "1" : "0"));
      f.println(prefix + "ShowHeading=" + String(w.showHeading ? "1" : "0"));
      f.println(prefix + "ShowBorder=" + String(w.showBorder ? "1" : "0"));
      f.println(prefix + "HeadingAlign=" + String(w.headingAlign)); f.println(prefix + "ContentAlign=" + String(w.contentAlign));
      f.println(prefix + "HeadingColor=" + String(w.headingColor)); f.println(prefix + "ContentColor=" + String(w.contentColor));
      f.println(prefix + "BackgroundColor=" + String(w.backgroundColor));
      f.println(prefix + "X=" + String(w.x)); f.println(prefix + "Y=" + String(w.y));
      f.println(prefix + "W=" + String(w.w)); f.println(prefix + "H=" + String(w.h));
      f.println(prefix + "Enabled=" + String(w.enabled ? "1" : "0"));
    }
  }
  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; i++) {
    f.println("wifi" + String(i) + "_ssid=" + savedNetworks[i].ssid);
    f.println("wifi" + String(i) + "_password=" + savedNetworks[i].password);
  }
  f.close();
  return true;
}
