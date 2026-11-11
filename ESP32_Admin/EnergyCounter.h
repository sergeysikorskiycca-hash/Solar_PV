/*
  Non-blocking lifetime energy accumulator for PV string sensor IDs 1..2.
  Only IDs that have actually reported telemetry are marked as detected.
*/
#pragma once
#include <Arduino.h>

class EnergyCounter {
public:
  static constexpr uint16_t SENSOR_COUNT = 2;

  void begin(uint32_t nowMs = 0);
  void update(uint8_t sensorId, float powerW, uint32_t nowMs);
  bool detected(uint8_t sensorId) const;
  uint16_t detectedCount() const;
  uint32_t stringWh(uint8_t sensorId) const;
  uint64_t totalWh() const;
  bool addStoredWh(uint8_t sensorId, uint32_t storedWh);
  bool restoreStoredWh(uint8_t sensorId, uint32_t storedWh);
  bool resetString(uint8_t sensorId);

private:
  uint32_t fullWh_[SENSOR_COUNT] = {};
  double fullRemainderWh_[SENSOR_COUNT] = {};
  uint32_t lastSampleMs_[SENSOR_COUNT] = {};
  float lastPowerW_[SENSOR_COUNT] = {};
  bool sampleSeen_[SENSOR_COUNT] = {};
  bool detected_[SENSOR_COUNT] = {};
  uint16_t detectedCount_ = 0;
};

extern EnergyCounter energyCounter;
