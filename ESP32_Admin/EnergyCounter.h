/*
  Non-blocking per-string and period energy accumulator.
  Energy is integrated from fresh sensor samples; persistence is handled separately.
*/
#pragma once
#include <Arduino.h>

class EnergyCounter {
public:
  static constexpr uint8_t SENSOR_COUNT = 10;
  void begin(uint32_t nowMs = 0);
  void update(uint8_t sensorId, float powerW, uint32_t nowMs);
  void setPeriodSelectionMask(uint16_t mask);
  uint16_t detectedMask() const;
  uint16_t periodSelectionMask() const;
  uint64_t stringWh(uint8_t sensorId) const;
  uint64_t periodWh() const;
  void resetPeriod();
  bool resetString(uint8_t sensorId);

private:
  uint64_t fullWh_[SENSOR_COUNT] = {};
  double fullRemainderWh_[SENSOR_COUNT] = {};
  uint32_t lastSampleMs_[SENSOR_COUNT] = {};
  float lastPowerW_[SENSOR_COUNT] = {};
  bool sampleSeen_[SENSOR_COUNT] = {};
  uint16_t detectedMask_ = 0;
  uint16_t periodSelectionMask_ = 0;
  uint64_t periodWh_ = 0;
  double periodRemainderWh_ = 0.0;
};

extern EnergyCounter energyCounter;
