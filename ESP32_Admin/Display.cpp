#include "Display.h"
#include "AppConfig.h"
#include <WiFi.h>

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
