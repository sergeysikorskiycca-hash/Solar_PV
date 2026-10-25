#pragma once
#include <Arduino.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "AppConfig.h"

extern Adafruit_ST7789 tft;
extern U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
extern bool apActive;

void drawCyrillicText(const String& text, int x, int baselineY, uint16_t color, bool large);
void drawTFTIndicators();
void renderTFT();
void drawWidget(const WidgetConfig& widget);
void updateWidgetIncremental(const WidgetConfig& widget, uint8_t pageIndex, uint8_t widgetIndex);
void resetIncrementalWidgetCache();
