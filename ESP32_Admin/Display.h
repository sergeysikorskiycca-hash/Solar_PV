#pragma once
#include <Arduino.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>

extern Adafruit_ST7789 tft;
extern U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
extern bool apActive;
extern String lastStatusText;

void drawCyrillicText(const String& text, int x, int baselineY, uint16_t color, bool large);
void drawCard(const String& label, const String& value, int x, int y, int w, int h);
String getTFTStatusText();
void drawTFTStatus(bool force = false);
void drawTFTIndicators();
void renderTFT();
