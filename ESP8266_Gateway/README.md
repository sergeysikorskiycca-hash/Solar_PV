# ESP8266 Gateway firmware

Companion sketch for `ESP32_Admin/ESP32_Admin.ino` on branch `esp32-admin-refactor`.

## I2C wiring

- ESP8266 NodeMCU: SDA = D2 / GPIO4, SCL = D1 / GPIO5.
- ESP32 Dev Module: SDA = GPIO21, SCL = GPIO22.
- DS3231: address `0x68`, connected to the same SDA/SCL bus.
- ESP32 endpoint: I2C slave address `0x42`.
- The browser admin and IR remote can set the DS3231 date/time through the ESP32; the gateway writes the command to RTC.
- Connect GND between both boards. Keep the bus at 3.3 V logic and ensure pull-ups do not pull SDA/SCL to 5 V.

The ESP8266 is the I2C master. It reads the DS3231 once per second and sends a CRC-checked packet to the ESP32 10 times per second. Each packet contains the time and the latest valid ESP-NOW sensor sample (sensor ID, voltage, current, power, shunt voltage, truncated 16-bit sensor packet number, and received ESP-NOW RSSI in dBm when available). The data frame is 32 bytes. Flags bit 2 indicates a valid RSSI field; bit 1 indicates valid sensor telemetry. The ESP32 reply is 12 bytes: the 3-byte ACK plus an optional one-shot RTC date/time write command.

## ESP-NOW sensors

- ESP-NOW channel: 1.
- The receive packet must match the existing sensor structure: `char name[20]; uint8_t sensorId; float voltage,current,power,shuntVoltage; uint32_t packetNumber;`
- Sensor IDs 1–10 are accepted. A sample is considered stale after 3 seconds and is no longer forwarded as valid.
- Sensor values are forwarded as floats. The ESP32 prints received time and sensor values to Serial at 115200 baud.
- This gateway sketch does not configure INA226 itself; it receives measurements from the ESP-NOW sensor node(s).

## Status indicators

- TFT Wi-Fi icon: green when ESP32 is connected to Wi-Fi, red when disconnected; green bars reflect RSSI.
- TFT I2C gateway icon: green only after a valid CRC-checked packet arrives from the ESP8266; red after 500 ms without a valid packet.
- TFT ESP-NOW indicator: green when a valid received RSSI value is forwarded; red if RSSI is unavailable or sensor data is stale.
- The indicators are independent and hidden while the IR remote text editor/menu is open.

## Flashing

1. Open `ESP8266_Gateway/ESP8266_Gateway.ino` in Arduino IDE and upload it to the ESP8266.
2. Open `ESP32_Admin/ESP32_Admin.ino` as the ESP32 sketch (keep the companion files in the same sketch folder) and upload it to the ESP32.
3. Open Serial Monitor at 115200 baud on the ESP8266 and ESP32 to verify DS3231 time, sensor packets, and I2C acknowledgements.
4. On the ESP32 TFT, check the top-right Wi-Fi and gateway indicators.

The source has not been compiled or hardware-tested here. Compile both sketches locally in Arduino IDE before uploading.
