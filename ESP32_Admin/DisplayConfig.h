#pragma once
#include <Arduino.h>

// TFT ST7789 wiring for a classic ESP32 Dev Module (ESP32-WROOM-32).
// Intended to keep all TFT signal wires on the same side of a common 30-pin board.
// Verify the silkscreen on the actual board before wiring; board layouts can vary.
//
// Hardware SPI: SCLK=GPIO18, MOSI=GPIO23. TFT does not use MISO.
// ST7789 control pins:
static constexpr uint8_t TFT_CS_PIN  = 16;
static constexpr uint8_t TFT_DC_PIN  = 17;
static constexpr uint8_t TFT_RST_PIN = 4;

// Connect TFT SCL -> GPIO18 and SDA -> GPIO23.
// Power, GND, and LEDA/LEDK backlight wiring are not defined here;
// check the TFT module's electrical requirements before connecting its backlight.
