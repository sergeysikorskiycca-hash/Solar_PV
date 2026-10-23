/*
  Solar_PV ESP8266 Gateway — I2C telemetry sender
  Board: NodeMCU / ESP-12E class ESP8266
  I2C bus: SDA=GPIO4 (D2), SCL=GPIO5 (D1), shared with DS3231 and ESP32.
  ESP8266 is the I2C master; ESP32 is slave address 0x42.
  Packet rate: 10 Hz. RTC is read once per second and cached.

  Sensor slots are reserved in the packet. They remain invalid until the
  actual sensor drivers are integrated; do not interpret zero as a reading.
*/

#include <Arduino.h>
#include <Wire.h>

static const uint8_t SDA_PIN = 4;        // D2 on NodeMCU
static const uint8_t SCL_PIN = 5;        // D1 on NodeMCU
static const uint8_t RTC_ADDRESS = 0x68; // DS3231
static const uint8_t ESP32_ADDRESS = 0x42;
static const uint8_t FRAME_MAGIC = 0xA7;
static const uint8_t FRAME_VERSION = 1;
static const uint8_t ACK_MAGIC = 0x5A;
static const uint32_t FRAME_INTERVAL_MS = 100; // 10 Hz
static const uint32_t RTC_INTERVAL_MS = 1000;  // RTC itself ticks once per second

#pragma pack(push, 1)
struct GatewayPacket {
  uint8_t magic;
  uint8_t version;
  uint8_t sequence;
  uint8_t flags;             // bit 0: RTC time valid
  uint16_t year;
  uint8_t month, day, hour, minute, second;
  int16_t sensor[4];         // reserved telemetry slots; units defined per sensor
  uint8_t sensorValidMask;   // bits 0..3 indicate valid sensor slots
  uint8_t crc;
};
#pragma pack(pop)
static_assert(sizeof(GatewayPacket) == 21, "GatewayPacket size mismatch");

static GatewayPacket packet = {};
static uint8_t sequenceNumber = 0;
static uint32_t lastFrameMs = 0;
static uint32_t lastRtcMs = 0;
static uint32_t lastLogMs = 0;
static bool rtcPresent = false;
static bool lastAckOk = false;
static uint8_t lastAckStatus = 0xFF;

static uint8_t crc8(const uint8_t* data, size_t length) {
  uint8_t crc = 0;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}

static uint8_t fromBcd(uint8_t value) {
  return (uint8_t)(((value >> 4) * 10) + (value & 0x0F));
}

static bool readRtc() {
  Wire.beginTransmission(RTC_ADDRESS);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission(false) != 0) return false;
  uint8_t received = Wire.requestFrom(RTC_ADDRESS, (uint8_t)7);
  if (received != 7) {
    while (Wire.available()) Wire.read();
    return false;
  }

  uint8_t sec = fromBcd(Wire.read() & 0x7F);
  uint8_t min = fromBcd(Wire.read() & 0x7F);
  uint8_t hour = fromBcd(Wire.read() & 0x3F); // 24-hour mode
  (void)Wire.read();                           // day of week
  uint8_t day = fromBcd(Wire.read() & 0x3F);
  uint8_t month = fromBcd(Wire.read() & 0x1F);
  uint16_t year = (uint16_t)(2000 + fromBcd(Wire.read()));

  if (sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || month < 1 || month > 12)
    return false;

  packet.year = year;
  packet.month = month;
  packet.day = day;
  packet.hour = hour;
  packet.minute = min;
  packet.second = sec;
  return true;
}

static bool exchangePacket() {
  packet.magic = FRAME_MAGIC;
  packet.version = FRAME_VERSION;
  packet.sequence = sequenceNumber++;
  packet.flags = rtcPresent ? 0x01 : 0x00;
  packet.sensorValidMask = 0; // sensors are deliberately marked unavailable until integrated
  packet.crc = crc8((const uint8_t*)&packet, sizeof(packet) - 1);

  Wire.beginTransmission(ESP32_ADDRESS);
  Wire.write((const uint8_t*)&packet, sizeof(packet));
  uint8_t writeError = Wire.endTransmission();
  if (writeError != 0) {
    lastAckOk = false;
    lastAckStatus = writeError;
    return false;
  }

  uint8_t count = Wire.requestFrom(ESP32_ADDRESS, (uint8_t)3);
  if (count != 3) {
    while (Wire.available()) Wire.read();
    lastAckOk = false;
    lastAckStatus = 0xFE;
    return false;
  }

  uint8_t ackMagic = Wire.read();
  uint8_t ackSequence = Wire.read();
  uint8_t ackStatus = Wire.read();
  lastAckStatus = ackStatus;
  lastAckOk = ackMagic == ACK_MAGIC && ackSequence == packet.sequence && ackStatus == 0;
  return lastAckOk;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println(F("Solar_PV ESP8266 Gateway — I2C telemetry"));

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  delay(50);

  rtcPresent = readRtc();
  lastRtcMs = millis();
  if (rtcPresent) Serial.println(F("DS3231 detected; RTC time will be sent to ESP32."));
  else Serial.println(F("WARNING: DS3231 not detected or time invalid; telemetry link will still be tested."));
}

void loop() {
  uint32_t now = millis();

  if (now - lastRtcMs >= RTC_INTERVAL_MS) {
    lastRtcMs = now;
    rtcPresent = readRtc();
  }

  if (now - lastFrameMs >= FRAME_INTERVAL_MS) {
    lastFrameMs = now;
    exchangePacket();
  }

  if (now - lastLogMs >= 1000UL) {
    lastLogMs = now;
    Serial.printf("I2C ESP32: %s | ACK status: 0x%02X | RTC: %s %04u-%02u-%02u %02u:%02u:%02u\n",
                  lastAckOk ? "OK" : "NO RESPONSE",
                  lastAckStatus,
                  rtcPresent ? "OK" : "MISSING",
                  packet.year, packet.month, packet.day,
                  packet.hour, packet.minute, packet.second);
  }

  yield();
}
