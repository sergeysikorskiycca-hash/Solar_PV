#include "Display.h"
#include "AppConfig.h"
#include <WiFi.h>

void drawCyrillicText(const String& text, int x, int baselineY, uint16_t color, bool large) {
  u8g2Fonts.setForegroundColor(color);
  u8g2Fonts.setFont(large ? u8g2_font_unifont_t_cyrillic : u8g2_font_6x13_t_cyrillic);
  u8g2Fonts.setCursor(x, baselineY);
  u8g2Fonts.print(text);
}

void drawCard(const String& label, const String& value, int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, 5, 0x29C6);
  tft.drawRoundRect(x, y, w, h, 5, 0x5B6D);
  drawCyrillicText(label, x + 6, y + 19, 0xBDF7, false);
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

void drawTFTStatus(bool force) {
  String status = getTFTStatusText();
  if (!force && status == lastStatusText) return;

  // Clear only the footer status area; do not flash the whole display.
  tft.fillRect(0, 296, 240, 24, ST77XX_BLACK);
  drawCyrillicText(status, 8, 313, 0xBDF7, false);
  lastStatusText = status;
}

void drawWidget(const WidgetConfig& widget) {
  if (!widget.enabled) return;
  const int x = widget.x, y = widget.y, w = widget.w, h = widget.h;
  tft.fillRoundRect(x, y, w, h, 5, 0x29C6);
  tft.drawRoundRect(x, y, w, h, 5, 0x5B6D);
  drawCyrillicText(widget.label, x + 6, y + 18, 0xBDF7, false);
  if (widget.type == "wifiStatus") {
    int lineY = y + 39;
    if (WiFi.status() == WL_CONNECTED) {
      drawCyrillicText("Сеть: " + cfg.ssid, x + 6, lineY, 0xFEA0, false); lineY += 23;
      drawCyrillicText("Сигнал: " + String(WiFi.RSSI()) + " dBm", x + 6, lineY, 0xBDF7, false); lineY += 23;
      drawCyrillicText("IP: " + WiFi.localIP().toString(), x + 6, lineY, 0xBDF7, false); lineY += 23;
    } else {
      drawCyrillicText("Роутер не подключён", x + 6, lineY, 0xF800, false); lineY += 23;
      drawCyrillicText("Сеть: " + cfg.ssid, x + 6, lineY, 0xBDF7, false); lineY += 23;
    }
    if (apActive && lineY < y + h - 8) {
      drawCyrillicText("Точка настройки:", x + 6, lineY, 0xBDF7, false); lineY += 22;
      if (lineY < y + h - 4) drawCyrillicText("IP: " + WiFi.softAPIP().toString(), x + 6, lineY, 0xBDF7, false);
    }
    return;
  }
  String value = "DEMO";
  if (widget.type == "power") value = "DEMO W";
  else if (widget.type == "voltage") value = "DEMO V";
  else if (widget.type == "current") value = "DEMO A";
  else if (widget.type == "battery") value = "DEMO %";
  drawCyrillicText(value, x + 6, y + 43, 0xFEA0, cfg.fontScale == 2);
}

void renderTFT() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  uint8_t pageIndex = cfg.tftPage > 0 ? cfg.tftPage - 1 : 0;
  if (pageIndex >= cfg.pageCount) pageIndex = 0;
  drawCyrillicText(cfg.title, 8, 25, ST77XX_WHITE, true);
  drawCyrillicText(cfg.pages[pageIndex].title, 8, 43, 0x07E0, false);
  PageConfig& page = cfg.pages[pageIndex];
  for (uint8_t i = 0; i < page.widgetCount; i++) drawWidget(page.widgets[i]);
  tft.drawFastHLine(8, 294, 224, 0x5B6D);
  lastStatusText = "";
  drawTFTStatus(true);
}
