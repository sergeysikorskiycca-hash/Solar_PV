# ESP8266 Gateway firmware

This sketch is the companion to `ESP32_Admin/ESP32_Admin.ino` on branch `esp32-admin-refactor`.

## I2C wiring

- ESP8266 (NodeMCU): SDA = D2 / GPIO4, SCL = D1 / GPIO5.
- ESP32 Dev Module: SDA = GPIO21, SCL = GPIO22.
- DS3231: address `0x68`, connected to the same SDA/SCL bus.
- ESP32 gateway endpoint: slave address `0x42`.
- Connect GND between both boards. Keep the bus at 3.3 V logic and ensure pull-ups do not pull SDA/SCL to 5 V.

The ESP8266 is the I2C master. It reads the DS3231 once per second and sends a CRC-checked packet to the ESP32 10 times per second. The ESP32 acknowledges the packet; the TFT gateway icon turns green only after a valid packet is received, otherwise it turns red after the communication timeout. The Wi-Fi icon is independent.

## Sensor slots

Four signed 16-bit sensor slots and a validity mask are already reserved in the packet. They are intentionally marked invalid in this first gateway build because the current ESP32 admin firmware does not yet define which physical sensors and units should populate those slots. Zero must not be treated as a sensor reading.

## Flashing

1. Open `ESP8266_Gateway/ESP8266_Gateway.ino` in Arduino IDE and upload it to the ESP8266.
2. Open `ESP32_Admin/ESP32_Admin.ino` as the ESP32 sketch (keep the companion files in the same sketch folder) and upload it to the ESP32.
3. Open Serial Monitor at 115200 baud on the ESP8266. It reports DS3231 status and whether the ESP32 acknowledges the frames.
4. On the ESP32 TFT, check the two top-right indicators: Wi-Fi and gateway are separate states.

This source update has not been compiled or hardware-tested here. Please compile both sketches locally in Arduino IDE before uploading.
