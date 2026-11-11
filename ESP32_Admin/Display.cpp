#include "Display.h"
#include "AppConfig.h"
#include "EnergyCounter.h"
#include <WiFi.h>

// Live telemetry received from the ESP8266 gateway over I2C.
extern bool gatewayOnline;
extern bool gatewaySensorValid;
extern float gatewayVoltage, gatewayCurrent, gatewayPower;
extern bool stringSensorValid[255];
extern uint32_t stringSensorLastSeenMs[255];
extern float stringSensorVoltage[255], stringSensorCurrent[255], stringSensorPower[255];

static bool stringSensorFresh(uint8_t index) {
  return index < 255 && stringSensorValid[index] &&
         millis() - stringSensorLastSeenMs[index] <= 3500UL;
}

extern bool wifiStatusInlineActive;
extern bool wifiStatusPendingRouter;
extern bool wifiStatusPendingAp;
extern uint8_t wifiStatusSelectedRow;
extern bool routerModeEnabled;
extern bool setupApEnabled;
extern bool apActive;
extern bool calibrationInlineActive;
extern bool calibrationEditing;
extern uint8_t calibrationRow;
extern uint8_t calibrationDigit;
extern uint32_t calibrationTargetScaled;

static const uint16_t WIDGET_COLORS[6] = { ST77XX_WHITE, 0x07FF, 0x07E0, 0xFFE0, 0xF800, 0xFD20 };
static const uint8_t FONT_HEIGHTS[6] = {13, 15, 16, 20, 24, 27};

static void setWidgetFont(uint8_t size) {
  switch (size) {
    case 0: u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic); break;       // 13 px
    case 1: u8g2Fonts.setFont(u8g2_font_9x15_t_cyrillic); break;       // 15 px
    case 2: u8g2Fonts.setFont(u8g2_font_unifont_t_cyrillic); break;   // 16 px
    case 3: u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic); break;    // 20 px
    case 4: u8g2Fonts.setFont(u8g2_font_inr24_t_cyrillic); break;    // 24 px
    default: u8g2Fonts.setFont(u8g2_font_inr27_t_cyrillic); break;   // 27 px
  }
}
static uint16_t widgetColor(uint8_t color) { return WIDGET_COLORS[color <= 5 ? color : 0]; }
static String calibrationTargetText(bool editing, bool selected, bool currentRow, const String& actualText) {
  // Outside the active edit, each string shows its own live measurement.
  // Only the selected row may display the shared temporary edit buffer.
  if (!editing || !selected) return actualText;
  char number[12];
  if (currentRow) {
    snprintf(number, sizeof(number), "%04lu.%02lu",
             (unsigned long)(calibrationTargetScaled / 1000UL),
             (unsigned long)((calibrationTargetScaled % 1000UL) / 10UL));
  } else {
    snprintf(number, sizeof(number), "%04lu.%01lu",
             (unsigned long)(calibrationTargetScaled / 1000UL),
             (unsigned long)((calibrationTargetScaled % 1000UL) / 100UL));
  }
  String result(number);
  const uint8_t digitCount = currentRow ? 6 : 5;
  if (calibrationDigit >= digitCount) return result;
  // Four integer digits, then the decimal point, then the fractional digits.
  const uint8_t position = calibrationDigit < 4 ? calibrationDigit : calibrationDigit + 1;
  result = result.substring(0, position) + "[" + result.charAt(position) + "]" + result.substring(position + 1);
  return result;
}
static uint8_t calibrationElementOrdinal(const WidgetElementConfig* target) {
  if (cfg.tftPage < 1 || cfg.tftPage > cfg.pageCount) return 255;
  const PageConfig& page = cfg.pages[cfg.tftPage - 1];
  uint8_t ordinal = 0;
  for (uint8_t i = 0; i < page.widgetCount; ++i) {
    const WidgetConfig& widget = page.widgets[i];
    if (!widget.enabled) continue;
    for (uint8_t e = 0; e < widget.elementCount; ++e) {
      const WidgetElementConfig& element = widget.elements[e];
      if (element.kind != ELEMENT_CALIBRATION) continue;
      if (&element == target) return ordinal;
      ++ordinal;
    }
  }
  return 255;
}
static String widgetElementText(const WidgetElementConfig& element) {
  const String prefix = element.label.length() ? element.label + ": " : "";
  if (element.kind == ELEMENT_TEXT) return element.label;
  if (element.kind == ELEMENT_TOTAL) {
    if (element.metric == 4)
      return prefix + String((double)energyCounter.totalWh() / 1000.0, 3) + " кВт·ч";
    float total = 0.0f;
    bool any = false;
    if (element.metric == 2) {
      for (uint16_t i = 0; i < 255; ++i) {
        if (stringSensorFresh((uint8_t)i)) { total += stringSensorPower[i]; any = true; }
      }
    } else if (element.metric >= 64) {
      const uint8_t sensorMask = element.metric - 64;
      for (uint8_t i = 0; i < 6; ++i) {
        if ((sensorMask & (1U << i)) && stringSensorFresh(i)) {
          total += stringSensorPower[i];
          any = true;
        }
      }
    }
    return prefix + (any ? String(total, 1) + " Вт" : String("— Вт"));
  }
  if (element.kind == ELEMENT_CLOCK) {
    extern bool gatewayTimeValid;
    extern uint16_t gatewayYear;
    extern uint8_t gatewayMonth, gatewayDay, gatewayHour, gatewayMinute, gatewaySecond;
    if (!gatewayTimeValid) return prefix + "--";
    String time = String(gatewayHour < 10 ? "0" : "") + String(gatewayHour) + ":" +
                  String(gatewayMinute < 10 ? "0" : "") + String(gatewayMinute) + ":" +
                  String(gatewaySecond < 10 ? "0" : "") + String(gatewaySecond);
    String date = String(gatewayDay < 10 ? "0" : "") + String(gatewayDay) + "." +
                  String(gatewayMonth < 10 ? "0" : "") + String(gatewayMonth) + "." + String(gatewayYear);
    return prefix + (element.metric == 1 ? date : (element.metric == 2 ? time + " " + date : time));
  }
  if (element.kind == ELEMENT_WIFI_STATUS) {
    if (wifiStatusInlineActive && (element.metric == 0 || element.metric == 4)) {
      const bool routerRow = element.metric == 0;
      const bool selected = wifiStatusSelectedRow == (routerRow ? 0 : 1);
      const bool visible = routerRow ? wifiStatusPendingRouter : wifiStatusPendingAp;
      return String(selected ? "> " : "  ") + prefix + (visible ? "ВКЛ" : "ВЫКЛ");
    }
    String value;
    switch (element.metric) {
      case 1: value = cfg.ssid.length() ? cfg.ssid : "—"; break;
      case 2: value = WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) + " dBm" : "—"; break;
      case 3: value = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "—"; break;
      case 4: value = apActive ? "ВКЛ" : "ВЫКЛ"; break;
      case 5: value = apActive ? WiFi.softAPIP().toString() : "—"; break;
      default: value = routerModeEnabled ? "ВКЛ" : "ВЫКЛ"; break;
    }
    return prefix + value;
  }
  if (element.kind == ELEMENT_WIFI_NETWORK)
    return prefix + (cfg.ssid.length() ? cfg.ssid : "не выбрана");
  if (element.kind == ELEMENT_CALIBRATION) {
    const uint8_t sensor = element.sourceId > 0 ? element.sourceId - 1 : 0;
    const bool fresh = sensor < 255 && stringSensorFresh(sensor);
    const String current = fresh ? String(stringSensorCurrent[sensor], 2) : "--";
    const String voltage = fresh ? String(stringSensorVoltage[sensor], 1) : "--";
    const uint8_t ordinal = calibrationElementOrdinal(&element);
    const bool selected = calibrationInlineActive && ordinal != 255 && calibrationRow / 2 == ordinal;
    const String currentText = calibrationTargetText(calibrationEditing, selected && calibrationRow % 2 == 0, true, current);
    const String voltageText = calibrationTargetText(calibrationEditing, selected && calibrationRow % 2 == 1, false, voltage);
    return prefix + currentText + " А / " + voltageText + " В";
  }

  const char* unit = element.metric == 1 ? "А" : (element.metric == 2 || element.metric == 5 || element.metric >= 64 ? "Вт" : (element.metric == 3 ? "%" : "В"));
  if (element.metric == 4 && element.sourceId == 0)
    return prefix + String((double)energyCounter.totalWh() / 1000.0, 3) + " кВт·ч";
  String value = "--";
  if (element.metric == 3) return prefix + "-- %";
  if (element.sourceId == 0 && element.metric == 5) {
    float total = 0.0f; bool any = false;
    for (uint16_t i = 0; i < 255; ++i) if (stringSensorFresh((uint8_t)i)) { total += stringSensorPower[i]; any = true; }
    if (any) return prefix + String(total, 1) + " Вт";
    return prefix + "— Вт";
  }
  if (element.sourceId == 0 && element.metric >= 64) {
    const uint8_t sensorMask = element.metric - 64;
    float total = 0.0f; bool any = false;
    for (uint8_t i = 0; i < 6; ++i) {
      if ((sensorMask & (1U << i)) && stringSensorFresh(i)) {
        total += stringSensorPower[i];
        any = true;
      }
    }
    return prefix + (any ? String(total, 1) + " Вт" : String("— Вт"));
  }
  if (element.sourceId == 0) {
    if (gatewayOnline && gatewaySensorValid) {
      if (element.metric == 1) value = String(gatewayCurrent, 2);
      else if (element.metric == 2) value = String(gatewayPower, 1);
      else value = String(gatewayVoltage, 1);
    }
  } else {
    const uint8_t sensor = element.sourceId - 1;
    if (element.metric == 4) {
      value = String((double)energyCounter.stringWh(element.sourceId) / 1000.0, 3);
      return prefix + value + " кВт·ч";
    }
    if (stringSensorFresh(sensor)) {
      if (element.metric == 1) value = String(stringSensorCurrent[sensor], 2);
      else if (element.metric == 2 || element.metric == 5) value = String(stringSensorPower[sensor], 1);
      else value = String(stringSensorVoltage[sensor], 1);
    }
  }
  return prefix + value + " " + unit;
}
static const uint16_t WIDGET_BACKGROUNDS[7] = { 0x0841, 0x001B, 0x0326, 0x3009, 0x4804, 0x3186, ST77XX_BLACK };
static uint16_t widgetBackground(uint8_t color) { return WIDGET_BACKGROUNDS[color <= 6 ? color : 0]; }
static int spacedTextWidth(const String& text, int8_t spacing) {
  int width = 0;
  uint8_t glyphCount = 0;
  for (unsigned int i = 0; i < text.length();) {
    const uint8_t lead = (uint8_t)text[i];
    uint8_t bytes = lead < 0x80 ? 1 : ((lead & 0xE0) == 0xC0 ? 2 : ((lead & 0xF0) == 0xE0 ? 3 : 4));
    if (i + bytes > text.length()) bytes = text.length() - i;
    const String glyph = text.substring(i, i + bytes);
    width += u8g2Fonts.getUTF8Width(glyph.c_str());
    ++glyphCount;
    i += bytes;
  }
  if (glyphCount > 1) width += (glyphCount - 1) * spacing;
  return width;
}
static void printSpacedText(const String& text, int x, int y, int8_t spacing) {
  if (spacing == 0) {
    u8g2Fonts.setCursor(x, y);
    u8g2Fonts.print(text);
    return;
  }
  int cursorX = x;
  for (unsigned int i = 0; i < text.length();) {
    const uint8_t lead = (uint8_t)text[i];
    uint8_t bytes = lead < 0x80 ? 1 : ((lead & 0xE0) == 0xC0 ? 2 : ((lead & 0xF0) == 0xE0 ? 3 : 4));
    if (i + bytes > text.length()) bytes = text.length() - i;
    const String glyph = text.substring(i, i + bytes);
    u8g2Fonts.setCursor(cursorX, y);
    u8g2Fonts.print(glyph);
    cursorX += u8g2Fonts.getUTF8Width(glyph.c_str()) + spacing;
    i += bytes;
  }
}
static void drawAlignedText(const String& text, int x, int y, int w, uint8_t size, uint8_t align, uint8_t color, int8_t letterSpacing) {
  setWidgetFont(size);
  u8g2Fonts.setForegroundColor(widgetColor(color));
  const int textWidth = spacedTextWidth(text, letterSpacing);
  int textX = x + 6, available = w - 12;
  if (align == 1) textX += (available - textWidth) / 2;
  else if (align == 2) textX += available - textWidth;
  if (textX < x + 3) textX = x + 3;
  printSpacedText(text, textX, y, letterSpacing);
}
void drawCyrillicText(const String& text, int x, int baselineY, uint16_t color, bool large) {
  u8g2Fonts.setForegroundColor(color);
  u8g2Fonts.setFont(large ? u8g2_font_unifont_t_cyrillic : u8g2_font_6x13_t_cyrillic);
  u8g2Fonts.setCursor(x, baselineY); u8g2Fonts.print(text);
}
void drawWidget(const WidgetConfig& widget) {
  if (!widget.enabled) return;
  const int x = widget.x, y = widget.y, w = widget.w, h = widget.h;
  tft.fillRoundRect(x, y, w, h, 5, widgetBackground(widget.backgroundColor));
  if (widget.showBorder) tft.drawRoundRect(x, y, w, h, 5, 0x5B6D);
  const uint8_t hs = widget.headingSize <= 5 ? widget.headingSize : 1;
  const uint8_t cs = widget.contentSize <= 5 ? widget.contentSize : 2;
  const int headingBaseline = y + FONT_HEIGHTS[hs] + 3;
  if (widget.showHeading) drawAlignedText(widget.label, x, headingBaseline, w, hs, widget.headingAlign, widget.headingColor, widget.headingLetterSpacing);
  const uint8_t count = widget.elementCount <= MAX_WIDGET_ELEMENTS ? widget.elementCount : MAX_WIDGET_ELEMENTS;
  uint8_t firstVisible = 0;
  firstVisible = 0;
  const uint8_t firstSize = firstVisible < count && widget.elements[firstVisible].fontSize <= 5 ? widget.elements[firstVisible].fontSize : cs;
  int lineY = widget.showHeading
      ? headingBaseline + FONT_HEIGHTS[hs] / 2 + FONT_HEIGHTS[firstSize] + 3
      : y + FONT_HEIGHTS[firstSize] + 5;
  for (uint8_t i = 0; i < count && lineY <= y + h - 4; ++i) {
    const WidgetElementConfig& element = widget.elements[i];
    const uint8_t size = element.fontSize <= 5 ? element.fontSize : cs;
    drawAlignedText(widgetElementText(element), x, lineY, w, size, widget.contentAlign, element.color, widget.contentLetterSpacing);
    lineY += FONT_HEIGHTS[size] + widget.rowSpacing;
  }
}
// Per-page/per-widget cache for incremental refresh of dynamic widget content.
struct IncrementalWidgetCache {
  bool initialized;
  int x, y, w, h, firstBaseline, lineHeight;
  uint8_t contentSize, align, color, background, count, rowSpacing;
  int8_t contentLetterSpacing;
  bool wifiEditing;
  String lines[18];
  uint16_t colors[18];
  uint8_t fontSizes[18];
};
static IncrementalWidgetCache incrementalCaches[MAX_PAGES][MAX_WIDGETS_PER_PAGE];

void updateWidgetIncremental(const WidgetConfig& widget, uint8_t pageIndex, uint8_t widgetIndex) {
  if (!widget.enabled || pageIndex >= MAX_PAGES || widgetIndex >= MAX_WIDGETS_PER_PAGE) return;
  IncrementalWidgetCache& cache = incrementalCaches[pageIndex][widgetIndex];
  const uint8_t headingSize = widget.headingSize <= 5 ? widget.headingSize : 1;
  const uint8_t contentSize = widget.contentSize <= 5 ? widget.contentSize : 2;
  const int headingBaseline = widget.y + FONT_HEIGHTS[headingSize] + 3;
  const uint8_t count = widget.elementCount <= MAX_WIDGET_ELEMENTS ? widget.elementCount : MAX_WIDGET_ELEMENTS;
  uint8_t firstVisible = 0;
  firstVisible = 0;
  const uint8_t firstSize = firstVisible < count && widget.elements[firstVisible].fontSize <= 5 ? widget.elements[firstVisible].fontSize : contentSize;
  const int firstBaseline = widget.showHeading
      ? headingBaseline + FONT_HEIGHTS[headingSize] / 2 + FONT_HEIGHTS[firstSize] + 3
      : widget.y + FONT_HEIGHTS[firstSize] + 5;
  bool layoutChanged = !cache.initialized || cache.x != widget.x || cache.y != widget.y ||
      cache.w != widget.w || cache.h != widget.h || cache.firstBaseline != firstBaseline ||
      cache.background != widget.backgroundColor || cache.count != count ||
      cache.align != widget.contentAlign || cache.rowSpacing != widget.rowSpacing || cache.contentLetterSpacing != widget.contentLetterSpacing || cache.wifiEditing != wifiStatusInlineActive;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t size = widget.elements[i].fontSize <= 5 ? widget.elements[i].fontSize : contentSize;
    if (!cache.initialized || cache.fontSizes[i] != size) layoutChanged = true;
  }
  if (layoutChanged) {
    const int top = firstBaseline - FONT_HEIGHTS[firstSize] + 1;
    const int bottom = widget.y + widget.h - 3;
    if (bottom > top) tft.fillRect(widget.x + 2, top, widget.w - 4, bottom - top, widgetBackground(widget.backgroundColor));
  }
  int lineY = firstBaseline;
  for (uint8_t i = 0; i < count; ++i) {
    const WidgetElementConfig& element = widget.elements[i];
    const uint8_t size = element.fontSize <= 5 ? element.fontSize : contentSize;
    const String line = widgetElementText(element);
    const uint16_t color = widgetColor(element.color);
    if (lineY <= widget.y + widget.h - 4 &&
        (layoutChanged || cache.lines[i] != line || cache.colors[i] != color)) {
      tft.fillRect(widget.x + 2, lineY - FONT_HEIGHTS[size] + 1,
                   widget.w - 4, FONT_HEIGHTS[size] + widget.rowSpacing, widgetBackground(widget.backgroundColor));
      drawAlignedText(line, widget.x, lineY, widget.w, size, widget.contentAlign, element.color, widget.contentLetterSpacing);
    }
    cache.lines[i] = line;
    cache.colors[i] = color;
    cache.fontSizes[i] = size;
    lineY += FONT_HEIGHTS[size] + widget.rowSpacing;
  }
  cache.x = widget.x; cache.y = widget.y; cache.w = widget.w; cache.h = widget.h;
  cache.firstBaseline = firstBaseline; cache.lineHeight = FONT_HEIGHTS[contentSize] + 4;
  cache.contentSize = contentSize; cache.align = widget.contentAlign;
  cache.color = widget.contentColor; cache.background = widget.backgroundColor;
  cache.count = count; cache.rowSpacing = widget.rowSpacing; cache.contentLetterSpacing = widget.contentLetterSpacing; cache.wifiEditing = wifiStatusInlineActive;
  cache.initialized = true;
}
void resetIncrementalWidgetCache() {
  for (uint8_t p = 0; p < MAX_PAGES; ++p)
    for (uint8_t w = 0; w < MAX_WIDGETS_PER_PAGE; ++w) incrementalCaches[p][w].initialized = false;
}

void drawTFTIndicators() {
  // Three independent indicators: ESP32 Wi-Fi, I2C gateway link, and fresh ESP-NOW sensor data.
  tft.fillRect(186, 0, 54, 23, ST77XX_BLACK);

  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const uint16_t wifiColor = wifiConnected ? 0x07E0 : 0xF800;
  int wifiBars = 0;
  if (wifiConnected) {
    const int rssi = WiFi.RSSI();
    wifiBars = rssi >= -55 ? 4 : (rssi >= -67 ? 3 : (rssi >= -78 ? 2 : 1));
  }
  for (int i = 0; i < 4; ++i) {
    const int h = 4 + i * 3;
    const uint16_t c = wifiConnected ? (i < wifiBars ? wifiColor : 0x4208) : wifiColor;
    tft.fillRect(188 + i * 4, 19 - h, 3, h, c);
  }

  // Gateway icon reports the freshness of the ESP8266 -> ESP32 I2C frames.
  const uint16_t linkColor = gatewayOnline ? 0x07E0 : 0xF800;
  tft.drawRoundRect(204, 5, 8, 12, 2, linkColor);
  tft.drawRoundRect(212, 5, 8, 12, 2, linkColor);
  tft.drawLine(210, 11, 214, 11, linkColor);
  tft.drawLine(211, 8, 213, 8, linkColor);
  tft.drawLine(211, 14, 213, 14, linkColor);

  // ESP-NOW has a distinct antenna symbol. Its color reports whether the gateway
  // has received sensor data recently; no RSSI measurement is needed.
  const bool espNowActive = gatewayOnline && gatewaySensorValid;
  const uint16_t espNowColor = espNowActive ? 0x07E0 : 0xF800;

  tft.drawLine(230, 10, 230, 19, espNowColor);
  tft.drawCircle(230, 7, 2, espNowColor);
  for (int i = 0; i < 4; ++i) {
    const int y = 11 + i * 2;
    const int len = 2 + i;
    tft.drawLine(230 - len, y, 229, y, espNowColor);
    tft.drawLine(231, y, 231 + len, y, espNowColor);
  }
}

void renderTFT() {
  resetIncrementalWidgetCache();
  // The TFT is reserved for the configured page widgets; network status belongs
  // in the dedicated Wi-Fi status widget, not in a permanent footer.
  tft.fillScreen(ST77XX_BLACK); tft.setTextWrap(false);
  uint8_t pageIndex=cfg.tftPage>0?cfg.tftPage-1:0; if(pageIndex>=cfg.pageCount)pageIndex=0;
  String pageTitle = cfg.pages[pageIndex].title;
  while (pageTitle.length() > 0 && u8g2Fonts.getUTF8Width(pageTitle.c_str()) > 172) {
    int cut = pageTitle.length() - 1;
    while (cut > 0 && (((uint8_t)pageTitle[cut] & 0xC0) == 0x80)) --cut;
    pageTitle.remove(cut);
  }
  if (cfg.pages[pageIndex].showTitle) drawCyrillicText(pageTitle,8,25,ST77XX_WHITE,true);
  PageConfig& page=cfg.pages[pageIndex]; for(uint8_t i=0;i<page.widgetCount;i++)drawWidget(page.widgets[i]);
  drawTFTIndicators();
}
