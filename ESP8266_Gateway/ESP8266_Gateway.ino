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
  uint16_t currentCalibrationX600;
  uint16_t voltageCalibrationX600;
  uint32_t packetNumber;
};

struct ESPNowCommand { uint8_t command; uint8_t sensorId; };
struct __attribute__((packed)) ESPNowCalibrationCommand { uint8_t command; uint8_t sensorId; float currentFactor; float voltageFactor; };

struct SensorSlot {
  uint8_t mac[6];
  char name[20];
  uint8_t sensorId;
  float voltage;
  float current;
  float power;
  float shuntVoltage;
  uint16_t currentCalibrationPermille;
  uint16_t voltageCalibrationPermille;
  uint32_t packetNumber;
  uint32_t lastSeenMs;
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
  float voltage, current, power;
  uint16_t currentCalibrationPermille;
  uint16_t voltageCalibrationPermille;
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
static uint8_t lastI2CWriteError = 0xFF;
static uint8_t lastI2CReadCount = 0;
static uint8_t lastAckMagicSeen = 0;
static uint8_t lastAckSequenceSeen = 0;
static bool espNowReady = false;
static uint8_t lastClockCommandSequence = 0xFF;
static uint8_t lastOtaCommandSequence = 0xFF;
static bool otaDispatchPending = false;
static uint8_t otaTargetSensorId = 1;
static uint32_t lastOtaDispatchMs = 0;
static uint8_t lastCalibrationCommandSequence = 0xFF;
static bool calibrationDispatchPending = false;
static ESPNowCalibrationCommand calibrationDispatch = {};
static uint32_t lastCalibrationDispatchMs = 0;

static uint8_t crc8(const uint8_t* data, size_t length) {
  uint8_t crc = 0;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}

static uint8_t toBcd(uint8_t value) { return (uint8_t)(((value / 10) << 4) | (value % 10)); }

static bool validCalendarDate(uint16_t year, uint8_t month, uint8_t day) {
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1) return false;
  static const uint8_t daysInMonth[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  uint8_t maxDay = daysInMonth[month - 1];
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) maxDay = 29;
  return day <= maxDay;
}

static bool writeRtc(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
  if (!validCalendarDate(year, month, day) || hour > 23 || minute > 59 || second > 59) return false;
  Wire.beginTransmission(RTC_ADDRESS);
  Wire.write((uint8_t)0x00);
  Wire.write(toBcd(second)); Wire.write(toBcd(minute)); Wire.write(toBcd(hour));
  Wire.write((uint8_t)1); // day of week (not otherwise used by the UI)
  Wire.write(toBcd(day)); Wire.write(toBcd(month)); Wire.write(toBcd((uint8_t)(year - 2000)));
  return Wire.endTransmission() == 0;
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
  if (len != sizeof(SensorPacket)) return;
  SensorPacket incoming;
  memcpy(&incoming, data, sizeof(incoming));
  if (incoming.sensorId < 1 || incoming.sensorId > MAX_SENSORS) return;

  SensorSlot& slot = sensorSlots[incoming.sensorId - 1];
  memcpy(slot.mac, mac, sizeof(slot.mac));
  memcpy(slot.name, incoming.name, sizeof(slot.name));
  slot.name[sizeof(slot.name) - 1] = '\0';
  slot.sensorId = incoming.sensorId;
  slot.voltage = incoming.voltage;
  slot.current = incoming.current;
  slot.power = incoming.power;
  slot.shuntVoltage = incoming.shuntVoltage;
  slot.currentCalibrationPermille = incoming.currentCalibrationPermille;
  slot.voltageCalibrationPermille = incoming.voltageCalibrationPermille;
  slot.packetNumber = incoming.packetNumber;
  slot.lastSeenMs = millis();
  slot.valid = true;
}

static int8_t chooseSensor() {
  for (uint8_t i = 0; i < MAX_SENSORS; ++i) {
    nextSensorIndex = (uint8_t)((nextSensorIndex + 1) % MAX_SENSORS);
    if (sensorSlots[nextSensorIndex].valid && millis() - sensorSlots[nextSensorIndex].lastSeenMs <= 3000UL) return (int8_t)nextSensorIndex;
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
    packet.currentCalibrationPermille = slot.currentCalibrationPermille;
    packet.voltageCalibrationPermille = slot.voltageCalibrationPermille;
    packet.sensorPacketNumber = (uint16_t)(slot.packetNumber & 0xFFFF);
  } else {
    packet.sensorId = 0;
    packet.voltage = 0.0f;
    packet.current = 0.0f;
    packet.power = 0.0f;
    packet.currentCalibrationPermille = 0;
    packet.voltageCalibrationPermille = 0;
    packet.sensorPacketNumber = 0;
  }

  packet.crc = crc8((const uint8_t*)&packet, sizeof(packet) - 1);
  Wire.beginTransmission(ESP32_ADDRESS);
  Wire.write((const uint8_t*)&packet, sizeof(packet));
  uint8_t writeError = Wire.endTransmission();
  lastI2CWriteError = writeError;
  if (writeError != 0) {
    lastAckOk = false;
    lastAckStatus = writeError;
    return false;
  }

  // Give the ESP32 slave receive callback a brief chance to validate the frame
  // and prepare the matching acknowledgement before the immediate read.
  delay(2);
  uint8_t count = Wire.requestFrom(ESP32_ADDRESS, (uint8_t)21);
  lastI2CReadCount = count;
  if (count != 21) {
    while (Wire.available()) Wire.read();
    lastAckOk = false;
    lastAckStatus = 0xFE;
    return false;
  }

  uint8_t ackMagic = Wire.read();
  uint8_t ackSequence = Wire.read();
  uint8_t ackStatus = Wire.read();
  lastAckMagicSeen = ackMagic;
  lastAckSequenceSeen = ackSequence;
  uint8_t commandType = Wire.read();
  uint8_t commandSequence = Wire.read();
  uint16_t commandYear = (uint16_t)Wire.read(); commandYear |= (uint16_t)Wire.read() << 8;
  uint8_t commandMonth = Wire.read(), commandDay = Wire.read(), commandHour = Wire.read(), commandMinute = Wire.read(), commandSecond = Wire.read();
  uint8_t calibrationSensorId = Wire.read();
  ESPNowCalibrationCommand incomingCalibration = {};
  incomingCalibration.command = 0x02;
  incomingCalibration.sensorId = calibrationSensorId;
  uint8_t* calibrationBytes = (uint8_t*)&incomingCalibration;
  for (uint8_t i = 2; i < sizeof(incomingCalibration); ++i) calibrationBytes[i] = Wire.read();
  lastAckStatus = ackStatus;
  lastAckOk = ackMagic == ACK_MAGIC && ackSequence == packet.sequence && ackStatus == 0;
  if (commandType == 1 && commandSequence != lastClockCommandSequence) {
    bool clockWritten = writeRtc(commandYear, commandMonth, commandDay, commandHour, commandMinute, commandSecond);
    if (clockWritten) { lastClockCommandSequence = commandSequence; rtcPresent = readRtc(); Serial.printf("RTC set from ESP32: %04u-%02u-%02u %02u:%02u:%02u\n", (unsigned)commandYear, (unsigned)commandMonth, (unsigned)commandDay, (unsigned)commandHour, (unsigned)commandMinute, (unsigned)commandSecond); }
    else Serial.println(F("ERROR: RTC write command failed."));
  } else if (commandType == 2 && commandSequence != lastOtaCommandSequence) {
    lastOtaCommandSequence = commandSequence;
    otaTargetSensorId = (uint8_t)(commandYear & 0xFF);
    otaDispatchPending = otaTargetSensorId >= 1 && otaTargetSensorId <= MAX_SENSORS;
    lastOtaDispatchMs = 0;
    Serial.printf("OTA request received from ESP32 for sensor %u.\n", (unsigned)otaTargetSensorId);
  } else if (commandType == 3 && commandSequence != lastCalibrationCommandSequence) {
    lastCalibrationCommandSequence = commandSequence;
    calibrationDispatch = incomingCalibration;
    calibrationDispatchPending = calibrationDispatch.sensorId >= 1 &&
      calibrationDispatch.sensorId <= MAX_SENSORS &&
      isfinite(calibrationDispatch.currentFactor) && isfinite(calibrationDispatch.voltageFactor);
    lastCalibrationDispatchMs = 0;
    Serial.printf("Calibration command received for sensor %u.\\n", (unsigned)calibrationDispatch.sensorId);
  }
  return lastAckOk;
}

static void processOtaDispatch() {
  if (!otaDispatchPending) return;
  uint32_t now = millis();
  if (now - lastOtaDispatchMs < 250UL) return;
  lastOtaDispatchMs = now;

  if (otaTargetSensorId < 1 || otaTargetSensorId > MAX_SENSORS) {
    otaDispatchPending = false;
    Serial.println(F("OTA request rejected: invalid sensor ID."));
    return;
  }

  SensorSlot& slot = sensorSlots[otaTargetSensorId - 1];
  if (!slot.valid || now - slot.lastSeenMs > 3000UL) return;

  if (!esp_now_is_peer_exist(slot.mac)) {
    int peerResult = esp_now_add_peer(slot.mac, ESP_NOW_ROLE_COMBO, ESP_NOW_CHANNEL, NULL, 0);
    if (peerResult != 0 && !esp_now_is_peer_exist(slot.mac)) {
      Serial.printf("OTA peer add failed: %d\n", peerResult);
      return;
    }
  }

  ESPNowCommand command = { 0x01, otaTargetSensorId };
  int sendResult = esp_now_send(slot.mac, (uint8_t*)&command, sizeof(command));
  if (sendResult == 0) {
    otaDispatchPending = false;
    Serial.printf("OTA command sent to sensor %u (%02X:%02X:%02X:%02X:%02X:%02X).\n",
                  (unsigned)otaTargetSensorId,
                  slot.mac[0], slot.mac[1], slot.mac[2],
                  slot.mac[3], slot.mac[4], slot.mac[5]);
  } else {
    Serial.printf("OTA send failed: %d; will retry.\n", sendResult);
  }
}

static void processCalibrationDispatch() {
  if (!calibrationDispatchPending || !espNowReady) return;
  uint32_t now = millis();
  if (now - lastCalibrationDispatchMs < 250UL) return;
  lastCalibrationDispatchMs = now;
  uint8_t id = calibrationDispatch.sensorId;
  if (id < 1 || id > MAX_SENSORS) { calibrationDispatchPending = false; return; }
  SensorSlot& slot = sensorSlots[id - 1];
  if (!slot.valid || now - slot.lastSeenMs > 3000UL) return;
  if (!esp_now_is_peer_exist(slot.mac)) {
    int peerResult = esp_now_add_peer(slot.mac, ESP_NOW_ROLE_COMBO, ESP_NOW_CHANNEL, NULL, 0);
    if (peerResult != 0 && !esp_now_is_peer_exist(slot.mac)) return;
  }
  int result = esp_now_send(slot.mac, (uint8_t*)&calibrationDispatch, sizeof(calibrationDispatch));
  if (result == 0) {
    calibrationDispatchPending = false;
    Serial.printf("Calibration command queued to sensor %u (I=%.3f U=%.3f); EEPROM confirmation is not available.\\n",
      (unsigned)id, calibrationDispatch.currentFactor, calibrationDispatch.voltageFactor);
  } else Serial.printf("Calibration ESP-NOW send error %d; retry pending.\\n", result);
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
  processOtaDispatch();
  processCalibrationDispatch();

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
    Serial.printf("DS3231: %s | %04u-%02u-%02u %02u:%02u:%02u\n",
                  rtcPresent ? "READ OK" : "READ FAILED",
                  (unsigned)packet.year, (unsigned)packet.month, (unsigned)packet.day,
                  (unsigned)packet.hour, (unsigned)packet.minute, (unsigned)packet.second);
    Serial.printf("I2C ESP32: %s | writeErr=%u | readBytes=%u/21 | ACK magic=0x%02X seq=%u status=0x%02X | ESP-NOW: %s\n",
                  lastAckOk ? "OK" : "NO RESPONSE",
                  (unsigned)lastI2CWriteError,
                  (unsigned)lastI2CReadCount,
                  (unsigned)lastAckMagicSeen,
                  (unsigned)lastAckSequenceSeen,
                  lastAckStatus,
                  espNowReady ? "READY" : "FAILED");
    int8_t sensorIndex = -1;
    for (uint8_t i = 0; i < MAX_SENSORS; ++i) if (sensorSlots[i].valid && millis() - sensorSlots[i].lastSeenMs <= 3000UL) { sensorIndex = (int8_t)i; break; }
    if (sensorIndex >= 0) {
      const SensorSlot& slot = sensorSlots[(uint8_t)sensorIndex];
      Serial.printf("Sensor %u (%s): %.2f V, %.3f A, %.1f W, K=%.3f KU=%.3f, packet %lu\n",
                    (unsigned)slot.sensorId, slot.name, slot.voltage, slot.current, slot.power,
                    slot.currentCalibrationPermille / 1000.0f,
                    slot.voltageCalibrationPermille / 1000.0f,
                    (unsigned long)slot.packetNumber);
    } else {
      Serial.println(F("No ESP-NOW sensor packet received yet."));
    }
  }

  yield();
}
