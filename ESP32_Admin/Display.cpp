#include "Display.h"
#include "AppConfig.h"
#include <WiFi.h>

// Live telemetry received from the ESP8266 gateway over I2C.
extern bool gatewayOnline;
extern bool gatewaySensorValid;
extern float gatewayVoltage, gatewayCurrent, gatewayPower;
extern bool stringSensorValid[10];
extern uint32_t stringSensorLastSeenMs[10];
extern float stringSensorVoltage[10], stringSensorCurrent[10], stringSensorPower[10];

static bool stringSensorFresh(uint8_t index) {
  return index < 10 && stringSensorValid[index] &&
         millis() - stringSensorLastSeenMs[index] <= 3500UL;
}

extern bool routerModeEnabled;
extern bool setupApEnabled;
extern bool wifiControlPendingRouter;
extern bool wifiControlPendingAp;
extern bool wifiControlApplyPending;
extern uint8_t wifiControlSelectedRow;
extern uint32_t wifiControlApplyAt;
extern bool calibrationInlineActive;
extern bool calibrationEditing;
extern uint8_t calibrationRow;
extern uint8_t calibrationDigit;
extern uint32_t calibrationTargetScaled;

static const uint16_t WIDGET_COLORS[6] = { ST77XX_WHITE, 0x07FF, 0x07E0, 0xFFE0, 0xF800, 0xFD20 };
static const uint8_t FONT_HEIGHTS[6] = {13, 14, 16, 15, 20, 24};

static void setWidgetFont(uint8_t size) {
  switch (size) {
    case 0: u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic); break;
    case 1: u8g2Fonts.setFont(u8g2_font_7x13_t_cyrillic); break;
    // Use the same Cyrillic-capable Unifont face as the page heading for the standard widget text.
    case 2: u8g2Fonts.setFont(u8g2_font_unifont_t_cyrillic); break;
    case 3: u8g2Fonts.setFont(u8g2_font_9x15_t_cyrillic); break;
    case 4: u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic); break;
    default: u8g2Fonts.setFont(u8g2_font_inr24_t_cyrillic); break;
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
static const uint16_t WIDGET_BACKGROUNDS[7] = { 0x0841, 0x001B, 0x0326, 0x3009, 0x4804, 0x3186, ST77XX_BLACK };
static uint16_t widgetBackground(uint8_t color) { return WIDGET_BACKGROUNDS[color <= 6 ? color : 0]; }
static void drawAlignedText(const String& text, int x, int y, int w, uint8_t size, uint8_t align, uint8_t color) {
  setWidgetFont(size);
  u8g2Fonts.setForegroundColor(widgetColor(color));
  int textWidth = u8g2Fonts.getUTF8Width(text.c_str());
  int textX = x + 6, available = w - 12;
  if (align == 1) textX += (available - textWidth) / 2;
  else if (align == 2) textX += available - textWidth;
  if (textX < x + 3) textX = x + 3;
  u8g2Fonts.setCursor(textX, y); u8g2Fonts.print(text);
}
void drawCyrillicText(const String& text, int x, int baselineY, uint16_t color, bool large) {
  u8g2Fonts.setForegroundColor(color);
  u8g2Fonts.setFont(large ? u8g2_font_unifont_t_cyrillic : u8g2_font_6x13_t_cyrillic);
  u8g2Fonts.setCursor(x, baselineY); u8g2Fonts.print(text);
}
static void drawWrappedText(const WidgetConfig& widget, const String& rawText, int startBaseline, int maxLines) {
  String content = rawText; content.replace("\r", ""); content += "\n";
  String line = ""; int baseline = startBaseline;
  uint8_t sz = widget.contentSize <= 5 ? widget.contentSize : 1;
  const int lineHeight = FONT_HEIGHTS[sz] + 3, maxWidth = widget.w - 12;
  setWidgetFont(sz);
  for (size_t pos = 0; pos < content.length() && maxLines > 0;) {
    if (content[pos] == '\n') {
      drawAlignedText(line, widget.x, baseline, widget.w, sz, widget.contentAlign, widget.contentColor);
      line = ""; baseline += lineHeight; maxLines--; pos++; continue;
    }
    uint8_t lead = (uint8_t)content[pos];
    size_t bytes = lead < 0x80 ? 1 : ((lead & 0xE0) == 0xC0 ? 2 : ((lead & 0xF0) == 0xE0 ? 3 : ((lead & 0xF8) == 0xF0 ? 4 : 1)));
    if (pos + bytes > content.length()) bytes = 1;
    String glyph = content.substring(pos, pos + bytes), candidate = line + glyph;
    if (line.length() && u8g2Fonts.getUTF8Width(candidate.c_str()) > maxWidth) {
      drawAlignedText(line, widget.x, baseline, widget.w, sz, widget.contentAlign, widget.contentColor);
      line = glyph; baseline += lineHeight; maxLines--;
    } else line = candidate;
    pos += bytes;
  }
  if (line.length() && maxLines > 0) drawAlignedText(line, widget.x, baseline, widget.w, sz, widget.contentAlign, widget.contentColor);
}
void drawWidget(const WidgetConfig& widget) {
  if (!widget.enabled) return;
  const int x=widget.x, y=widget.y, w=widget.w, h=widget.h;
  tft.fillRoundRect(x,y,w,h,5,widgetBackground(widget.backgroundColor));
  if (widget.showBorder) tft.drawRoundRect(x,y,w,h,5,0x5B6D);
  uint8_t hs=widget.headingSize<=5?widget.headingSize:1, cs=widget.contentSize<=5?widget.contentSize:2;
  int headingBaseline=y+FONT_HEIGHTS[hs]+3;
  if (widget.showHeading) drawAlignedText(widget.label,x,headingBaseline,w,hs,widget.headingAlign,widget.headingColor);
  int contentBaseline=widget.showHeading ? headingBaseline+FONT_HEIGHTS[hs]/2+FONT_HEIGHTS[cs]+3 : y+FONT_HEIGHTS[cs]+5;
  if(widget.type=="calibration") {
    const uint8_t strings = widget.stringCount >= 2 && widget.stringCount <= 6 ? widget.stringCount : 2;
    const int lineHeight = FONT_HEIGHTS[cs] + 3; int lineY = contentBaseline;
    for (uint8_t s = 0; s < strings; ++s) {
      drawAlignedText(cfg.uiTexts.stringPrefix + " " + String(s + 1), x, lineY, w, cs, widget.contentAlign, widget.headingColor); lineY += lineHeight;
      const uint8_t currentRow = s * 2, voltageRow = currentRow + 1;
      const bool currentSelected = calibrationInlineActive && calibrationRow == currentRow;
      const bool voltageSelected = calibrationInlineActive && calibrationRow == voltageRow;
      const bool fresh = stringSensorFresh(s);
      const String currentActual = fresh ? String(stringSensorCurrent[s], 2) : "--";
      const String voltageActual = fresh ? String(stringSensorVoltage[s], 1) : "--";
      drawAlignedText(String(currentSelected ? (calibrationEditing ? "* " : "> ") : "  ") +
                      cfg.uiTexts.currentLabel + " " + currentActual + " > " + calibrationTargetText(calibrationEditing, currentSelected, true, currentActual) + " А",
                      x + 3, lineY, w - 3, cs, widget.contentAlign,
                      currentSelected ? 3 : widget.contentColor); lineY += lineHeight;
      drawAlignedText(String(voltageSelected ? (calibrationEditing ? "* " : "> ") : "  ") +
                      cfg.uiTexts.voltageLabel + " " + voltageActual + " > " + calibrationTargetText(calibrationEditing, voltageSelected, false, voltageActual) + " В",
                      x + 3, lineY, w - 3, cs, widget.contentAlign,
                      voltageSelected ? 3 : widget.contentColor); lineY += lineHeight;
    }
    return;
  }
  if(widget.type=="clock") {
    extern bool gatewayTimeValid; extern uint16_t gatewayYear; extern uint8_t gatewayMonth, gatewayDay, gatewayHour, gatewayMinute, gatewaySecond;
    String timeText = gatewayTimeValid ? (String(gatewayHour < 10 ? "0" : "") + String(gatewayHour) + ":" + String(gatewayMinute < 10 ? "0" : "") + String(gatewayMinute) + ":" + String(gatewaySecond < 10 ? "0" : "") + String(gatewaySecond)) : String("--:--:--");
    String dateText = gatewayTimeValid ? (String(gatewayDay < 10 ? "0" : "") + String(gatewayDay) + "." + String(gatewayMonth < 10 ? "0" : "") + String(gatewayMonth) + "." + String(gatewayYear)) : String("--.--.----");
    drawAlignedText(timeText,x,contentBaseline,w,cs,widget.contentAlign,widget.contentColor);
    drawAlignedText(dateText,x,contentBaseline+FONT_HEIGHTS[cs]+4,w,cs,widget.contentAlign,widget.contentColor);
    return;
  }
  if(widget.type=="text") {
    int avail=y+h-4-contentBaseline, lineHeight=FONT_HEIGHTS[cs]+3;
    drawWrappedText(widget,widget.text,contentBaseline,avail>0?avail/lineHeight+1:0); return;
  }
  if(widget.type=="strings" || widget.type=="stringsVoltage" ||
     widget.type=="stringsCurrent" || widget.type=="stringsPower") {
    const char* unit = "В";
    if (widget.type == "stringsCurrent") unit = "А";
    else if (widget.type == "stringsPower") unit = "Вт";
    int lineY = contentBaseline, lineHeight = FONT_HEIGHTS[cs] + 4;
    uint8_t count = widget.stringCount >= 1 && widget.stringCount <= 6 ? widget.stringCount : 2;
    for (uint8_t i = 0; i < count && lineY <= y + h - 4; i++) {
      String reading = "--";
      if (stringSensorFresh(i)) {
        if (widget.type == "stringsCurrent") reading = String(stringSensorCurrent[i], 2);
        else if (widget.type == "stringsPower") reading = String(stringSensorPower[i], 1);
        else reading = String(stringSensorVoltage[i], 1);
      }
      drawAlignedText(cfg.uiTexts.stringPrefix + " " + String(i + 1) + ": " + reading + " " + unit,
                      x, lineY, w, cs, widget.contentAlign, widget.contentColor);
      lineY += lineHeight;
    }
    return;
  }
  if(widget.type=="wifiControl") {
    int lineY=contentBaseline, lineHeight=FONT_HEIGHTS[cs]+4;
    auto line=[&](const String& s,uint16_t color){if(lineY<=y+h-4)drawAlignedText(s,x,lineY,w,cs,widget.contentAlign,color);lineY+=lineHeight;};
    bool router = wifiControlApplyPending ? wifiControlPendingRouter : routerModeEnabled;
    bool ap = wifiControlApplyPending ? wifiControlPendingAp : setupApEnabled;
    line(String(wifiControlSelectedRow==0?"> ":"  ")+cfg.uiTexts.routerLabel + ": "+(router?"ВКЛ":"ВЫКЛ"),wifiControlSelectedRow==0?0x07FF:(router?0x07E0:0xF800));
    line(String(wifiControlSelectedRow==1?"> ":"  ")+cfg.uiTexts.apLabel + ": "+(ap?"ВКЛ":"ВЫКЛ"),wifiControlSelectedRow==1?0x07FF:(ap?0x07E0:0xF800));
    if(wifiControlApplyPending) {
      uint32_t remaining = (int32_t)(wifiControlApplyAt - millis()) > 0 ? (wifiControlApplyAt - millis() + 999UL) / 1000UL : 0;
      line(cfg.uiTexts.applyLabel + " "+String(remaining)+" сек.",0xFFE0);
    } else {
      line(cfg.uiTexts.selectHint,0xBDF7);
      line(cfg.uiTexts.changeHint,0xBDF7);
    }
    return;
  }
  if(widget.type=="wifiStatus") {
    int lineY=contentBaseline, lineHeight=FONT_HEIGHTS[cs]+5;
    auto drawLine=[&](const String& value){if(lineY<=y+h-4)drawAlignedText(value,x,lineY,w,cs,widget.contentAlign,widget.contentColor);lineY+=lineHeight;};
    if(WiFi.status()==WL_CONNECTED){drawLine(cfg.uiTexts.networkLabel + ": "+cfg.ssid);drawLine(cfg.uiTexts.signalLabel + ": "+String(WiFi.RSSI())+" dBm");drawLine("IP: "+WiFi.localIP().toString());}
    else {drawLine(cfg.uiTexts.noRouterLabel);drawLine(cfg.uiTexts.networkLabel + ": "+cfg.ssid);}
    if(apActive&&lineY<y+h-4){drawLine(cfg.uiTexts.apSetupLabel);drawLine("IP: "+WiFi.softAPIP().toString());} return;
  }
  String value = "--";
  if (widget.type == "power") {
    // Show the sum of the latest received values from String 1 and String 2.
    // Keep the last readings until a new packet updates the corresponding string.
    value = (stringSensorValid[0] || stringSensorValid[1])
                ? String(stringSensorPower[0] + stringSensorPower[1], 1) + " W"
                : "-- W";
  } else if (widget.type == "voltage") {
    value = gatewayOnline && gatewaySensorValid ? String(gatewayVoltage, 1) + " V" : "-- V";
  } else if (widget.type == "current") {
    value = gatewayOnline && gatewaySensorValid ? String(gatewayCurrent, 2) + " A" : "-- A";
  } else if (widget.type == "battery") {
    value = "-- %"; // Battery percentage is not supplied by the PV sensor.
  }
  drawAlignedText(value,x,contentBaseline,w,cs,widget.contentAlign,widget.contentColor);
}
// Per-page/per-widget cache for incremental refresh of dynamic widget content.
struct IncrementalWidgetCache {
  bool initialized;
  int x, y, w, h, firstBaseline, lineHeight;
  uint8_t contentSize, align, color, background, count;
  String lines[18];
  uint16_t colors[18];
};
static IncrementalWidgetCache incrementalCaches[MAX_PAGES][MAX_WIDGETS_PER_PAGE];

static void makeDynamicLines(const WidgetConfig& widget, String out[], uint16_t colors[], uint8_t& count) {
  count = 0;
  if (widget.type == "clock") {
    extern bool gatewayTimeValid;
    extern uint16_t gatewayYear;
    extern uint8_t gatewayMonth, gatewayDay, gatewayHour, gatewayMinute, gatewaySecond;
    out[count] = gatewayTimeValid ? (String(gatewayHour < 10 ? "0" : "") + String(gatewayHour) + ":" + String(gatewayMinute < 10 ? "0" : "") + String(gatewayMinute) + ":" + String(gatewaySecond < 10 ? "0" : "") + String(gatewaySecond)) : "--:--:--";
    colors[count++] = widgetColor(widget.contentColor);
    out[count] = gatewayTimeValid ? (String(gatewayDay < 10 ? "0" : "") + String(gatewayDay) + "." + String(gatewayMonth < 10 ? "0" : "") + String(gatewayMonth) + "." + String(gatewayYear)) : "--.--.----";
    colors[count++] = widgetColor(widget.contentColor);
  } else if (widget.type == "calibration") {
    const uint8_t strings = widget.stringCount >= 2 && widget.stringCount <= 6 ? widget.stringCount : 2;
    for (uint8_t s = 0; s < strings && count + 3 <= 18; ++s) {
      out[count] = cfg.uiTexts.stringPrefix + " " + String(s + 1); colors[count++] = widgetColor(widget.headingColor);
      const uint8_t currentRow = s * 2, voltageRow = currentRow + 1;
      const bool currentSelected = calibrationInlineActive && calibrationRow == currentRow;
      const bool voltageSelected = calibrationInlineActive && calibrationRow == voltageRow;
      const bool fresh = stringSensorFresh(s);
      const String currentActual = fresh ? String(stringSensorCurrent[s], 2) : "--";
      const String voltageActual = fresh ? String(stringSensorVoltage[s], 1) : "--";
      out[count] = String(currentSelected ? (calibrationEditing ? "* " : "> ") : "  ") +
                   cfg.uiTexts.currentLabel + " " + currentActual + " > " + calibrationTargetText(calibrationEditing, currentSelected, true, currentActual) + " А";
      colors[count++] = currentSelected ? 0xFFE0 : widgetColor(widget.contentColor);
      out[count] = String(voltageSelected ? (calibrationEditing ? "* " : "> ") : "  ") +
                   cfg.uiTexts.voltageLabel + " " + voltageActual + " > " + calibrationTargetText(calibrationEditing, voltageSelected, false, voltageActual) + " В";
      colors[count++] = voltageSelected ? 0xFFE0 : widgetColor(widget.contentColor);
    }
  } else if (widget.type == "wifiStatus") {
    if (WiFi.status() == WL_CONNECTED) {
      out[count] = cfg.uiTexts.networkLabel + ": " + cfg.ssid; colors[count++] = widgetColor(widget.contentColor);
      out[count] = cfg.uiTexts.signalLabel + ": " + String(WiFi.RSSI()) + " dBm"; colors[count++] = widgetColor(widget.contentColor);
      out[count] = "IP: " + WiFi.localIP().toString(); colors[count++] = widgetColor(widget.contentColor);
    } else {
      out[count] = cfg.uiTexts.noRouterLabel; colors[count++] = widgetColor(widget.contentColor);
      out[count] = "Сеть: " + cfg.ssid; colors[count++] = widgetColor(widget.contentColor);
    }
    if (apActive && count <= 4) {
      out[count] = cfg.uiTexts.apSetupLabel; colors[count++] = widgetColor(widget.contentColor);
      out[count] = "IP: " + WiFi.softAPIP().toString(); colors[count++] = widgetColor(widget.contentColor);
    }
  } else if (widget.type == "wifiControl") {
    bool router = wifiControlApplyPending ? wifiControlPendingRouter : routerModeEnabled;
    bool ap = wifiControlApplyPending ? wifiControlPendingAp : setupApEnabled;
    out[count] = String(wifiControlSelectedRow == 0 ? "> " : "  ") + cfg.uiTexts.routerLabel + ": " + (router ? "ВКЛ" : "ВЫКЛ");
    colors[count++] = wifiControlSelectedRow == 0 ? 0x07FF : (router ? 0x07E0 : 0xF800);
    out[count] = String(wifiControlSelectedRow == 1 ? "> " : "  ") + cfg.uiTexts.apLabel + ": " + (ap ? "ВКЛ" : "ВЫКЛ");
    colors[count++] = wifiControlSelectedRow == 1 ? 0x07FF : (ap ? 0x07E0 : 0xF800);
    if (wifiControlApplyPending) {
      uint32_t remaining = (int32_t)(wifiControlApplyAt - millis()) > 0 ? (wifiControlApplyAt - millis() + 999UL) / 1000UL : 0;
      out[count] = "Применение через " + String(remaining) + " сек."; colors[count++] = 0xFFE0;
    } else {
      out[count] = cfg.uiTexts.selectHint; colors[count++] = 0xBDF7;
      out[count] = cfg.uiTexts.changeHint; colors[count++] = 0xBDF7;
    }
  } else if (widget.type == "strings" || widget.type == "stringsVoltage" ||
             widget.type == "stringsCurrent" || widget.type == "stringsPower") {
    const char* unit = widget.type == "stringsCurrent" ? "А" :
                       (widget.type == "stringsPower" ? "Вт" : "В");
    const uint8_t stringCount = widget.stringCount >= 1 && widget.stringCount <= 6 ? widget.stringCount : 2;
    for (uint8_t i = 0; i < stringCount && count < 6; ++i) {
      String reading = "--";
      if (stringSensorFresh(i)) {
        if (widget.type == "stringsCurrent") reading = String(stringSensorCurrent[i], 2);
        else if (widget.type == "stringsPower") reading = String(stringSensorPower[i], 1);
        else reading = String(stringSensorVoltage[i], 1);
      }
      out[count] = cfg.uiTexts.stringPrefix + " " + String(i + 1) + ": " + reading + " " + unit;
      colors[count++] = widgetColor(widget.contentColor);
    }
  } else if (widget.type == "power" || widget.type == "voltage" || widget.type == "current" || widget.type == "battery") {
    if (widget.type == "power") {
      // Sum the latest stored power values; do not expire a string between packets.
      out[count] = (stringSensorValid[0] || stringSensorValid[1])
                       ? String(stringSensorPower[0] + stringSensorPower[1], 1) + " W"
                       : "-- W";
    } else if (widget.type == "voltage") {
      out[count] = gatewayOnline && gatewaySensorValid ? String(gatewayVoltage, 1) + " V" : "-- V";
    } else if (widget.type == "current") {
      out[count] = gatewayOnline && gatewaySensorValid ? String(gatewayCurrent, 2) + " A" : "-- A";
    } else {
      out[count] = "-- %"; // Battery percentage is not supplied by the PV sensor.
    }
    colors[count++] = widgetColor(widget.contentColor);
  }
}

void updateWidgetIncremental(const WidgetConfig& widget, uint8_t pageIndex, uint8_t widgetIndex) {
  if (!widget.enabled || pageIndex >= MAX_PAGES || widgetIndex >= MAX_WIDGETS_PER_PAGE) return;
  if (widget.type == "text") return;
  String lines[18]; uint16_t colors[18]; uint8_t count = 0;
  makeDynamicLines(widget, lines, colors, count);
  if (!count) return;
  const uint8_t hs = widget.headingSize <= 5 ? widget.headingSize : 1;
  const uint8_t cs = widget.contentSize <= 5 ? widget.contentSize : 2;
  const int headingBaseline = widget.y + FONT_HEIGHTS[hs] + 3;
  const int firstBaseline = widget.showHeading ? headingBaseline + FONT_HEIGHTS[hs] / 2 + FONT_HEIGHTS[cs] + 3 : widget.y + FONT_HEIGHTS[cs] + 5;
  const int lineHeight = FONT_HEIGHTS[cs] + (widget.type == "wifiStatus" ? 5 : 4);
  IncrementalWidgetCache& cache = incrementalCaches[pageIndex][widgetIndex];
  const bool layoutChanged = !cache.initialized || cache.x != widget.x || cache.y != widget.y || cache.w != widget.w ||
      cache.h != widget.h || cache.firstBaseline != firstBaseline || cache.lineHeight != lineHeight ||
      cache.contentSize != cs || cache.align != widget.contentAlign || cache.color != widget.contentColor ||
      cache.background != widget.backgroundColor || cache.count != count;
  if (layoutChanged) {
    int top = firstBaseline - FONT_HEIGHTS[cs] + 1, bottom = widget.y + widget.h - 3;
    if (bottom > top) tft.fillRect(widget.x + 2, top, widget.w - 4, bottom - top, widgetBackground(widget.backgroundColor));
  }
  for (uint8_t i = 0; i < count; ++i) {
    int baseline = firstBaseline + i * lineHeight;
    if (baseline > widget.y + widget.h - 4) break;
    if (layoutChanged || cache.lines[i] != lines[i] || cache.colors[i] != colors[i]) {
      tft.fillRect(widget.x + 2, baseline - FONT_HEIGHTS[cs] + 1, widget.w - 4, FONT_HEIGHTS[cs] + 4, widgetBackground(widget.backgroundColor));
      setWidgetFont(cs);
      int textWidth = u8g2Fonts.getUTF8Width(lines[i].c_str()), tx = widget.x + 6, available = widget.w - 12;
      if (widget.contentAlign == 1) tx += (available - textWidth) / 2;
      else if (widget.contentAlign == 2) tx += available - textWidth;
      if (tx < widget.x + 3) tx = widget.x + 3;
      u8g2Fonts.setForegroundColor(colors[i]); u8g2Fonts.setCursor(tx, baseline); u8g2Fonts.print(lines[i]);
    }
    cache.lines[i] = lines[i]; cache.colors[i] = colors[i];
  }
  cache.x = widget.x; cache.y = widget.y; cache.w = widget.w; cache.h = widget.h;
  cache.firstBaseline = firstBaseline; cache.lineHeight = lineHeight; cache.contentSize = cs;
  cache.align = widget.contentAlign; cache.color = widget.contentColor; cache.background = widget.backgroundColor;
  cache.count = count; cache.initialized = true;
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
