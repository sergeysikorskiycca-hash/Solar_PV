#include "Display.h"
#include "AppConfig.h"
#include <WiFi.h>
extern bool routerModeEnabled;
extern bool setupApEnabled;
extern bool wifiControlPendingRouter;
extern bool wifiControlPendingAp;
extern bool wifiControlApplyPending;
extern uint8_t wifiControlSelectedRow;
extern uint32_t wifiControlApplyAt;

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
void drawCard(const String& label, const String& value, int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, 5, 0x29C6); tft.drawRoundRect(x, y, w, h, 5, 0x5B6D);
  drawCyrillicText(label, x + 6, y + 19, 0xBDF7, false);
  drawCyrillicText(String(value), x + 6, y + 43, 0xFEA0, cfg.fontScale == 2);
}
String getTFTStatusText() {
  if (WiFi.status() == WL_CONNECTED) return "Wi-Fi " + String(WiFi.RSSI()) + " dBm " + WiFi.localIP().toString();
  if (apActive) return "Точка настройки: " + WiFi.softAPIP().toString();
  return "Подключение к Wi-Fi...";
}
void drawTFTStatus(bool force) {
  String status = getTFTStatusText(); if (!force && status == lastStatusText) return;
  tft.fillRect(0, 296, 240, 24, ST77XX_BLACK); drawCyrillicText(status, 8, 313, 0xBDF7, false); lastStatusText = status;
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
  if(widget.type=="strings") {
    // The layout supports multiple strings now; values remain placeholders until telemetry is connected.
    int lineY = contentBaseline, lineHeight = FONT_HEIGHTS[cs] + 4;
    uint8_t count = widget.stringCount >= 1 && widget.stringCount <= 6 ? widget.stringCount : 2;
    for (uint8_t i = 0; i < count && lineY <= y + h - 4; i++) {
      drawAlignedText("Стринг " + String(i + 1) + ": -- В", x, lineY, w, cs, widget.contentAlign, widget.contentColor);
      lineY += lineHeight;
    }
    return;
  }
  if(widget.type=="wifiControl") {
    int lineY=contentBaseline, lineHeight=FONT_HEIGHTS[cs]+4;
    auto line=[&](const String& s,uint16_t color){if(lineY<=y+h-4)drawAlignedText(s,x,lineY,w,cs,widget.contentAlign,color);lineY+=lineHeight;};
    bool router = wifiControlApplyPending ? wifiControlPendingRouter : routerModeEnabled;
    bool ap = wifiControlApplyPending ? wifiControlPendingAp : setupApEnabled;
    line(String(wifiControlSelectedRow==0?"> ":"  ")+"Роутер: "+(router?"ВКЛ":"ВЫКЛ"),wifiControlSelectedRow==0?0x07FF:(router?0x07E0:0xF800));
    line(String(wifiControlSelectedRow==1?"> ":"  ")+"Точка: "+(ap?"ВКЛ":"ВЫКЛ"),wifiControlSelectedRow==1?0x07FF:(ap?0x07E0:0xF800));
    if(wifiControlApplyPending) {
      uint32_t remaining = (int32_t)(wifiControlApplyAt - millis()) > 0 ? (wifiControlApplyAt - millis() + 999UL) / 1000UL : 0;
      line("Применение через "+String(remaining)+" сек.",0xFFE0);
    } else {
      line("ВВЕРХ/ВНИЗ: выбор",0xBDF7);
      line("OK: изменить (5 сек.)",0xBDF7);
    }
    return;
  }
  if(widget.type=="wifiStatus") {
    int lineY=contentBaseline, lineHeight=FONT_HEIGHTS[cs]+5;
    auto drawLine=[&](const String& value){if(lineY<=y+h-4)drawAlignedText(value,x,lineY,w,cs,widget.contentAlign,widget.contentColor);lineY+=lineHeight;};
    if(WiFi.status()==WL_CONNECTED){drawLine("Сеть: "+cfg.ssid);drawLine("Сигнал: "+String(WiFi.RSSI())+" dBm");drawLine("IP: "+WiFi.localIP().toString());}
    else {drawLine("Роутер не подключён");drawLine("Сеть: "+cfg.ssid);}
    if(apActive&&lineY<y+h-4){drawLine("Точка настройки:");drawLine("IP: "+WiFi.softAPIP().toString());} return;
  }
  String value="DEMO";
  if(widget.type=="power")value="DEMO W";else if(widget.type=="voltage")value="DEMO V";else if(widget.type=="current")value="DEMO A";else if(widget.type=="battery")value="DEMO %";
  drawAlignedText(value,x,contentBaseline,w,cs,widget.contentAlign,widget.contentColor);
}
extern bool gatewayOnline;

void updateClockWidget(const WidgetConfig& widget) {
  if (!widget.enabled || widget.type != "clock") return;

  extern bool gatewayTimeValid;
  extern uint16_t gatewayYear;
  extern uint8_t gatewayMonth, gatewayDay, gatewayHour, gatewayMinute, gatewaySecond;

  const int x = widget.x, y = widget.y, w = widget.w;
  const uint8_t hs = widget.headingSize <= 5 ? widget.headingSize : 1;
  const uint8_t cs = widget.contentSize <= 5 ? widget.contentSize : 2;
  const int headingBaseline = y + FONT_HEIGHTS[hs] + 3;
  const int contentBaseline = widget.showHeading
      ? headingBaseline + FONT_HEIGHTS[hs] / 2 + FONT_HEIGHTS[cs] + 3
      : y + FONT_HEIGHTS[cs] + 5;
  const int dateBaseline = contentBaseline + FONT_HEIGHTS[cs] + 4;

  String timeText = gatewayTimeValid
      ? (String(gatewayHour < 10 ? "0" : "") + String(gatewayHour) + ":" +
         String(gatewayMinute < 10 ? "0" : "") + String(gatewayMinute) + ":" +
         String(gatewaySecond < 10 ? "0" : "") + String(gatewaySecond))
      : String("--:--:--");
  String dateText = gatewayTimeValid
      ? (String(gatewayDay < 10 ? "0" : "") + String(gatewayDay) + "." +
         String(gatewayMonth < 10 ? "0" : "") + String(gatewayMonth) + "." +
         String(gatewayYear))
      : String("--.--.----");

  // Keep a separate cache for each clock widget. Redraw only changed glyphs:
  // seconds normally change just two digits, while the date stays untouched.
  struct ClockCache {
    int x, y, w, contentBaseline, dateBaseline;
    uint8_t size, align, color, background;
    String timeText, dateText;
    bool initialized;
  };
  static ClockCache caches[MAX_PAGES * MAX_WIDGETS_PER_PAGE] = {};
  static int cacheSlot = 0;
  // A widget's stable screen position is used to locate its cache slot.
  int slot = -1;
  for (int i = 0; i < MAX_PAGES * MAX_WIDGETS_PER_PAGE; ++i) {
    if (caches[i].initialized && caches[i].x == x && caches[i].y == y &&
        caches[i].w == w && caches[i].size == cs) { slot = i; break; }
  }
  if (slot < 0) {
    slot = cacheSlot++ % (MAX_PAGES * MAX_WIDGETS_PER_PAGE);
    caches[slot].initialized = false;
  }

  setWidgetFont(cs);
  u8g2Fonts.setForegroundColor(widgetColor(widget.contentColor));
  auto drawChangedLine = [&](const String& oldText, const String& newText, int baseline) {
    const int available = w - 12;
    const int newWidth = u8g2Fonts.getUTF8Width(newText.c_str());
    int startX = x + 6;
    if (widget.contentAlign == 1) startX += (available - newWidth) / 2;
    else if (widget.contentAlign == 2) startX += available - newWidth;
    if (startX < x + 3) startX = x + 3;

    if (oldText.length() != newText.length()) {
      tft.fillRect(x + 2, baseline - FONT_HEIGHTS[cs] + 1, w - 4,
                   FONT_HEIGHTS[cs] + 4, widgetBackground(widget.backgroundColor));
      drawAlignedText(newText, x, baseline, w, cs, widget.contentAlign, widget.contentColor);
      return;
    }

    for (size_t i = 0; i < newText.length(); ++i) {
      if (oldText[i] == newText[i]) continue;
      String prefix = newText.substring(0, i);
      String oldGlyph = oldText.substring(i, i + 1);
      String newGlyph = newText.substring(i, i + 1);
      const int glyphX = startX + u8g2Fonts.getUTF8Width(prefix.c_str());
      const int oldWidth = u8g2Fonts.getUTF8Width(oldGlyph.c_str());
      const int newGlyphWidth = u8g2Fonts.getUTF8Width(newGlyph.c_str());
      const int clearWidth = max(oldWidth, newGlyphWidth) + 2;
      tft.fillRect(glyphX - 1, baseline - FONT_HEIGHTS[cs] + 1,
                   clearWidth, FONT_HEIGHTS[cs] + 4,
                   widgetBackground(widget.backgroundColor));
      u8g2Fonts.setCursor(glyphX, baseline);
      u8g2Fonts.print(newGlyph);
    }
  };

  ClockCache& cache = caches[slot];
  const bool layoutChanged = !cache.initialized || cache.x != x || cache.y != y ||
      cache.w != w || cache.contentBaseline != contentBaseline ||
      cache.dateBaseline != dateBaseline || cache.size != cs ||
      cache.align != widget.contentAlign || cache.color != widget.contentColor ||
      cache.background != widget.backgroundColor;
  if (layoutChanged) {
    tft.fillRect(x + 2, contentBaseline - FONT_HEIGHTS[cs] + 1, w - 4,
                 (dateBaseline - contentBaseline) + FONT_HEIGHTS[cs] + 4,
                 widgetBackground(widget.backgroundColor));
    drawAlignedText(timeText, x, contentBaseline, w, cs, widget.contentAlign, widget.contentColor);
    drawAlignedText(dateText, x, dateBaseline, w, cs, widget.contentAlign, widget.contentColor);
  } else {
    if (cache.timeText != timeText) drawChangedLine(cache.timeText, timeText, contentBaseline);
    if (cache.dateText != dateText) drawChangedLine(cache.dateText, dateText, dateBaseline);
  }

  cache.x = x; cache.y = y; cache.w = w;
  cache.contentBaseline = contentBaseline; cache.dateBaseline = dateBaseline;
  cache.size = cs; cache.align = widget.contentAlign; cache.color = widget.contentColor;
  cache.background = widget.backgroundColor;
  cache.timeText = timeText; cache.dateText = dateText; cache.initialized = true;
}


// Per-page/per-widget cache for incremental refresh of dynamic widget content.
struct IncrementalWidgetCache {
  bool initialized;
  int x, y, w, h, firstBaseline, lineHeight;
  uint8_t contentSize, align, color, background, count;
  String lines[6];
  uint16_t colors[6];
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
  } else if (widget.type == "wifiStatus") {
    if (WiFi.status() == WL_CONNECTED) {
      out[count] = "Сеть: " + cfg.ssid; colors[count++] = widgetColor(widget.contentColor);
      out[count] = "Сигнал: " + String(WiFi.RSSI()) + " dBm"; colors[count++] = widgetColor(widget.contentColor);
      out[count] = "IP: " + WiFi.localIP().toString(); colors[count++] = widgetColor(widget.contentColor);
    } else {
      out[count] = "Роутер не подключён"; colors[count++] = widgetColor(widget.contentColor);
      out[count] = "Сеть: " + cfg.ssid; colors[count++] = widgetColor(widget.contentColor);
    }
    if (apActive && count <= 4) {
      out[count] = "Точка настройки:"; colors[count++] = widgetColor(widget.contentColor);
      out[count] = "IP: " + WiFi.softAPIP().toString(); colors[count++] = widgetColor(widget.contentColor);
    }
  } else if (widget.type == "wifiControl") {
    bool router = wifiControlApplyPending ? wifiControlPendingRouter : routerModeEnabled;
    bool ap = wifiControlApplyPending ? wifiControlPendingAp : setupApEnabled;
    out[count] = String(wifiControlSelectedRow == 0 ? "> " : "  ") + "Роутер: " + (router ? "ВКЛ" : "ВЫКЛ");
    colors[count++] = wifiControlSelectedRow == 0 ? 0x07FF : (router ? 0x07E0 : 0xF800);
    out[count] = String(wifiControlSelectedRow == 1 ? "> " : "  ") + "Точка: " + (ap ? "ВКЛ" : "ВЫКЛ");
    colors[count++] = wifiControlSelectedRow == 1 ? 0x07FF : (ap ? 0x07E0 : 0xF800);
    if (wifiControlApplyPending) {
      uint32_t remaining = (int32_t)(wifiControlApplyAt - millis()) > 0 ? (wifiControlApplyAt - millis() + 999UL) / 1000UL : 0;
      out[count] = "Применение через " + String(remaining) + " сек."; colors[count++] = 0xFFE0;
    } else {
      out[count] = "ВВЕРХ/ВНИЗ: выбор"; colors[count++] = 0xBDF7;
      out[count] = "OK: изменить (5 сек.)"; colors[count++] = 0xBDF7;
    }
  } else if (widget.type == "power" || widget.type == "voltage" || widget.type == "current" || widget.type == "battery") {
    out[count] = widget.type == "power" ? "DEMO W" : widget.type == "voltage" ? "DEMO V" : widget.type == "current" ? "DEMO A" : "DEMO %";
    colors[count++] = widgetColor(widget.contentColor);
  }
}

void updateWidgetIncremental(const WidgetConfig& widget, uint8_t pageIndex, uint8_t widgetIndex) {
  if (!widget.enabled || pageIndex >= MAX_PAGES || widgetIndex >= MAX_WIDGETS_PER_PAGE) return;
  if (widget.type == "text" || widget.type == "strings") return;
  String lines[6]; uint16_t colors[6]; uint8_t count = 0;
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
  // Reserve the top-right corner for independent Wi-Fi and gateway status.
  tft.fillRect(186, 0, 54, 23, ST77XX_BLACK);
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const uint16_t wifiColor = wifiConnected ? 0x07E0 : 0xF800;
  int bars = 0;
  if (wifiConnected) {
    int rssi = WiFi.RSSI();
    bars = rssi >= -55 ? 4 : (rssi >= -67 ? 3 : (rssi >= -78 ? 2 : 1));
  }
  for (int i = 0; i < 4; ++i) {
    int h = 4 + i * 3;
    uint16_t c = (wifiConnected && i < bars) || !wifiConnected ? wifiColor : 0x4208;
    tft.fillRect(188 + i * 4, 19 - h, 3, h, c);
  }

  const uint16_t linkColor = gatewayOnline ? 0x07E0 : 0xF800;
  tft.drawRoundRect(207, 5, 10, 12, 2, linkColor);
  tft.drawRoundRect(225, 5, 10, 12, 2, linkColor);
  tft.drawLine(217, 11, 225, 11, linkColor);
  tft.drawLine(219, 8, 223, 8, linkColor);
  tft.drawLine(219, 14, 223, 14, linkColor);
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
  lastStatusText="";
}
