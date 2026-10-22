#include "Display.h"
#include "AppConfig.h"
#include <WiFi.h>

static const uint16_t WIDGET_COLORS[6] = { ST77XX_WHITE, 0x07FF, 0x07E0, 0xFFE0, 0xF800, 0xFD20 };
static const uint8_t FONT_HEIGHTS[5] = {13, 14, 16, 15, 20};

static void setWidgetFont(uint8_t size) {
  switch (size) {
    case 0: u8g2Fonts.setFont(u8g2_font_6x13_t_cyrillic); break;
    case 1: u8g2Fonts.setFont(u8g2_font_7x13_t_cyrillic); break;
    // Use the same Cyrillic-capable Unifont face as the page heading for the standard widget text.
    case 2: u8g2Fonts.setFont(u8g2_font_unifont_t_cyrillic); break;
    case 3: u8g2Fonts.setFont(u8g2_font_9x15_t_cyrillic); break;
    default: u8g2Fonts.setFont(u8g2_font_10x20_t_cyrillic); break;
  }
}
static uint16_t widgetColor(uint8_t color) { return WIDGET_COLORS[color <= 5 ? color : 0]; }
static const uint16_t WIDGET_BACKGROUNDS[6] = { 0x0841, 0x001B, 0x0326, 0x3009, 0x4804, 0x3186 };
static uint16_t widgetBackground(uint8_t color) { return WIDGET_BACKGROUNDS[color <= 5 ? color : 0]; }
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
  uint8_t sz = widget.contentSize <= 4 ? widget.contentSize : 1;
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
  tft.fillRoundRect(x,y,w,h,5,widgetBackground(widget.backgroundColor)); tft.drawRoundRect(x,y,w,h,5,0x5B6D);
  uint8_t hs=widget.headingSize<=4?widget.headingSize:1, cs=widget.contentSize<=4?widget.contentSize:2;
  int headingBaseline=y+FONT_HEIGHTS[hs]+3;
  drawAlignedText(widget.label,x,headingBaseline,w,hs,widget.headingAlign,widget.headingColor);
  int contentBaseline=headingBaseline+FONT_HEIGHTS[hs]/2+FONT_HEIGHTS[cs]+3;
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
void renderTFT() {
  tft.fillScreen(ST77XX_BLACK); tft.setTextWrap(false);
  uint8_t pageIndex=cfg.tftPage>0?cfg.tftPage-1:0; if(pageIndex>=cfg.pageCount)pageIndex=0;
  drawCyrillicText(cfg.pages[pageIndex].title,8,25,ST77XX_WHITE,true);
  PageConfig& page=cfg.pages[pageIndex]; for(uint8_t i=0;i<page.widgetCount;i++)drawWidget(page.widgets[i]);
  tft.drawFastHLine(8,294,224,0x5B6D); lastStatusText=""; drawTFTStatus(true);
}
