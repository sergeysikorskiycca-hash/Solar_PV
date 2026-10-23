/*
  Solar_PV ESP8266 Gateway
  Board: NodeMCU / ESP-12E class ESP8266
  I2C: SDA=GPIO4 (D2), SCL=GPIO5 (D1), shared with DS3231 and ESP32.
  ESP8266 is the I2C master; ESP32 is slave address 0x42.
  ESP-NOW sensor channel: 1. The gateway forwards the latest sensor sample
  together with cached DS3231 time at 10 Hz.

  Sensor packet layout must match the existing SOLAR_STRING_1 sender:
  char name[20]; uint8_t sensorId; float voltage,current,power,shuntVoltage;
  uint32_t packetNumber;
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <string.h>
extern "C" {
  #include <espnow.h>
  #include <user_interface.h>
}

static const uint8_t SDA_PIN = 4;        // D2 on NodeMCU
static const uint8_t SCL_PIN = 5;        // D1 on NodeMCU
static const uint8_t RTC_ADDRESS = 0x68; // DS3231
static const uint8_t ESP32_ADDRESS = 0x42;
static const uint8_t ESP_NOW_CHANNEL = 1;
static const uint8_t FRAME_MAGIC = 0xA7;
static const uint8_t FRAME_VERSION = 1;
static const uint8_t ACK_MAGIC = 0x5A;
static const uint32_t FRAME_INTERVAL_MS = 100; // 10 Hz
static const uint32_t RTC_INTERVAL_MS = 1000;  // DS3231 changes once per second
static const uint8_t MAX_SENSORS = 10;

struct SensorPacket {
  char name[20];
  uint8_t sensorId;
  float voltage;
  float current;
  float power;
  float shuntVoltage;
  uint32_t packetNumber;
};

struct SensorSlot {
  char name[20];
  uint8_t sensorId;
  float voltage;
  float current;
  float power;
  float shuntVoltage;
  uint32_t packetNumber;
  bool valid;
};

#pragma pack(push, 1)
struct GatewayPacket {
  uint8_t magic;
  uint8_t version;
  uint8_t sequence;
  uint8_t flags;             // bit 0: RTC valid, bit 1: sensor data valid
  uint16_t year;
  uint8_t month, day, hour, minute, second;
  uint8_t sensorId;
  float voltage, current, power, shuntVoltage;
  uint16_t sensorPacketNumber;
  uint8_t crc;
};
#pragma pack(pop)
static_assert(sizeof(GatewayPacket) == 31, "GatewayPacket size mismatch; keep frame <= 32 bytes");

static SensorSlot sensorSlots[MAX_SENSORS] = {};
static GatewayPacket packet = {};
static uint8_t sequenceNumber = 0;
static uint8_t nextSensorIndex = 0;
static uint32_t lastFrameMs = 0;
static uint32_t lastRtcMs = 0;
static uint32_t lastLogMs = 0;
static bool rtcPresent = false;
static bool lastAckOk = false;
static uint8_t lastAckStatus = 0xFF;
static bool espNowReady = false;

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
  uint8_t rawHour = Wire.read();
  uint8_t hour;
  if (rawHour & 0x40) { // DS3231 12-hour mode
    hour = fromBcd(rawHour & 0x1F);
    bool pm = (rawHour & 0x20) != 0;
    if (hour == 12) hour = 0;
    if (pm) hour += 12;
  } else {
    hour = fromBcd(rawHour & 0x3F);
  }
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

static void onSensorData(uint8_t* mac, uint8_t* data, uint8_t len) {
  (void)mac;
  if (len != sizeof(SensorPacket)) return;
  SensorPacket incoming;
  memcpy(&incoming, data, sizeof(incoming));
  if (incoming.sensorId < 1 || incoming.sensorId > MAX_SENSORS) return;

  SensorSlot& slot = sensorSlots[incoming.sensorId - 1];
  memcpy(slot.name, incoming.name, sizeof(slot.name));
  slot.name[sizeof(slot.name) - 1] = '\0';
  slot.sensorId = incoming.sensorId;
  slot.voltage = incoming.voltage;
  slot.current = incoming.current;
  slot.power = incoming.power;
  slot.shuntVoltage = incoming.shuntVoltage;
  slot.packetNumber = incoming.packetNumber;
  slot.valid = true;
}

static int8_t chooseSensor() {
  for (uint8_t i = 0; i < MAX_SENSORS; ++i) {
    nextSensorIndex = (uint8_t)((nextSensorIndex + 1) % MAX_SENSORS);
    if (sensorSlots[nextSensorIndex].valid) return (int8_t)nextSensorIndex;
  }
  return -1;
}

static bool exchangePacket() {
  int8_t sensorIndex = chooseSensor();
  packet.magic = FRAME_MAGIC;
  packet.version = FRAME_VERSION;
  packet.sequence = sequenceNumber++;
  packet.flags = rtcPresent ? 0x01 : 0x00;

  if (sensorIndex >= 0) {
    const SensorSlot& slot = sensorSlots[(uint8_t)sensorIndex];
    packet.flags |= 0x02;
    packet.sensorId = slot.sensorId;
    packet.voltage = slot.voltage;
    packet.current = slot.current;
    packet.power = slot.power;
    packet.shuntVoltage = slot.shuntVoltage;
    packet.sensorPacketNumber = (uint16_t)(slot.packetNumber & 0xFFFF);
  } else {
    packet.sensorId = 0;
    packet.voltage = 0.0f;
    packet.current = 0.0f;
    packet.power = 0.0f;
    packet.shuntVoltage = 0.0f;
    packet.sensorPacketNumber = 0;
  }

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
  Serial.println(F("Solar_PV ESP8266 Gateway — ESP-NOW + I2C"));

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  wifi_set_channel(ESP_NOW_CHANNEL);
  if (esp_now_init() == 0) {
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(onSensorData);
    espNowReady = true;
    Serial.println(F("ESP-NOW ready on channel 1; waiting for sensor packets."));
  } else {
    Serial.println(F("ERROR: ESP-NOW init failed."));
  }

  delay(50);
  rtcPresent = readRtc();
  lastRtcMs = millis();
  if (rtcPresent) Serial.println(F("DS3231 detected; RTC time will be sent to ESP32."));
  else Serial.println(F("WARNING: DS3231 not detected or time invalid; link test will continue."));
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
    Serial.printf("I2C ESP32: %s | ACK status: 0x%02X | RTC: %s %04u-%02u-%02u %02u:%02u:%02u | ESP-NOW: %s\n",
                  lastAckOk ? "OK" : "NO RESPONSE",
                  lastAckStatus,
                  rtcPresent ? "OK" : "MISSING",
                  (unsigned)packet.year, (unsigned)packet.month, (unsigned)packet.day,
                  (unsigned)packet.hour, (unsigned)packet.minute, (unsigned)packet.second,
                  espNowReady ? "READY" : "FAILED");
    int8_t sensorIndex = -1;
    for (uint8_t i = 0; i < MAX_SENSORS; ++i) if (sensorSlots[i].valid) { sensorIndex = (int8_t)i; break; }
    if (sensorIndex >= 0) {
      const SensorSlot& slot = sensorSlots[(uint8_t)sensorIndex];
      Serial.printf("Sensor %u (%s): %.2f V, %.3f A, %.1f W, shunt %.3f mV, packet %lu\n",
                    (unsigned)slot.sensorId, slot.name, slot.voltage, slot.current, slot.power,
                    slot.shuntVoltage, (unsigned long)slot.packetNumber);
    } else {
      Serial.println(F("No ESP-NOW sensor packet received yet."));
    }
  }

  yield();
}
