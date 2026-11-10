/*
  Non-blocking energy integration for up to ten discovered PV string sensors.
  Each string total is independent from the selected period total.
*/
#include "EnergyCounter.h"
#include <math.h>

EnergyCounter energyCounter;

void EnergyCounter::begin(uint32_t nowMs) {
  (void)nowMs;
}

void EnergyCounter::update(uint8_t sensorId, float powerW, uint32_t nowMs) {
  if (sensorId < 1 || sensorId > SENSOR_COUNT || !isfinite(powerW)) return;
  const uint8_t index = sensorId - 1;
  detectedMask_ |= (uint16_t)(1U << index);

  if (!sampleSeen_[index]) {
    sampleSeen_[index] = true;
    lastSampleMs_[index] = nowMs;
    lastPowerW_[index] = powerW;
    return;
  }

  const uint32_t elapsedMs = nowMs - lastSampleMs_[index];
  lastSampleMs_[index] = nowMs;

  // Integrate the previous sample over its elapsed interval, then retain the new sample.
  const float intervalPowerW = lastPowerW_[index];
  lastPowerW_[index] = powerW;

  // A delayed or missing sensor packet must not create a large energy jump.
  if (elapsedMs == 0 || elapsedMs > 3000UL || intervalPowerW < 1.0f) return;

  const double addedWh = (double)intervalPowerW * (double)elapsedMs / 3600000.0;
  const double fullTotal = fullRemainderWh_[index] + addedWh;
  const uint64_t wholeFullWh = (uint64_t)fullTotal;
  if (wholeFullWh) {
    fullWh_[index] += wholeFullWh;
    fullRemainderWh_[index] = fullTotal - (double)wholeFullWh;
  } else {
    fullRemainderWh_[index] = fullTotal;
  }

  if (periodSelectionMask_ & (uint16_t)(1U << index)) {
    const double periodTotal = periodRemainderWh_ + addedWh;
    const uint64_t wholePeriodWh = (uint64_t)periodTotal;
    if (wholePeriodWh) {
      periodWh_ += wholePeriodWh;
      periodRemainderWh_ = periodTotal - (double)wholePeriodWh;
    } else {
      periodRemainderWh_ = periodTotal;
    }
  }
}

void EnergyCounter::setPeriodSelectionMask(uint16_t mask) {
  const uint16_t allowed = (uint16_t)((1U << SENSOR_COUNT) - 1U);
  periodSelectionMask_ = mask & allowed;
}

uint16_t EnergyCounter::detectedMask() const { return detectedMask_; }
uint16_t EnergyCounter::periodSelectionMask() const { return periodSelectionMask_; }

uint64_t EnergyCounter::stringWh(uint8_t sensorId) const {
  if (sensorId < 1 || sensorId > SENSOR_COUNT) return 0;
  return fullWh_[sensorId - 1];
}

uint64_t EnergyCounter::periodWh() const { return periodWh_; }

void EnergyCounter::resetPeriod() {
  periodWh_ = 0;
  periodRemainderWh_ = 0.0;
}

bool EnergyCounter::resetString(uint8_t sensorId) {
  if (sensorId < 1 || sensorId > SENSOR_COUNT) return false;
  const uint8_t index = sensorId - 1;
  fullWh_[index] = 0;
  fullRemainderWh_[index] = 0.0;
  return true;
}
