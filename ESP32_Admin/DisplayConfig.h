#pragma once
#include <Arduino.h>

// Default GPIOs for a classic ESP32 Dev Module (ESP32-WROOM-32).
// These are a configurable starting point, NOT a claim about the user's current wiring.
// Hardware SPI defaults: SCLK=GPIO18, MOSI=GPIO23. MISO is not used by this TFT.
static constexpr uint8_t TFT_CS_PIN  = 5;
static constexpr uint8_t TFT_DC_PIN  = 16;
static constexpr uint8_t TFT_RST_PIN = 17;
