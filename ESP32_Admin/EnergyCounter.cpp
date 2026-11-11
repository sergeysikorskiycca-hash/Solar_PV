/*
  Non-blocking energy integration. Each sensor ID has an independent lifetime
  counter; the aggregate is calculated from the per-string totals.
*/
#include "EnergyCounter.h"
#include <math.h>
#include <limits.h>

EnergyCounter energyCounter;

void EnergyCounter::begin(uint32_t nowMs) {
  (void)nowMs;
}

void EnergyCounter::update(uint8_t sensorId, float powerW, uint32_t nowMs) {
  if (sensorId == 0 || sensorId > SENSOR_COUNT || !isfinite(powerW)) return;
  const uint16_t index = (uint16_t)sensorId - 1U;

  if (!detected_[index]) {
    detected_[index] = true;
    ++detectedCount_;
  }

  if (powerW < 0.0f) powerW = 0.0f;

  if (!sampleSeen_[index]) {
    sampleSeen_[index] = true;
    lastSampleMs_[index] = nowMs;
    lastPowerW_[index] = powerW;
    return;
  }

  const uint32_t elapsedMs = nowMs - lastSampleMs_[index];
  const float intervalPowerW = lastPowerW_[index];
  lastSampleMs_[index] = nowMs;
  lastPowerW_[index] = powerW;

  if (elapsedMs == 0 || elapsedMs > 3000UL || intervalPowerW <= 0.0f ||
      fullWh_[index] == UINT32_MAX) return;

  const double addedWh = (double)intervalPowerW * (double)elapsedMs / 3600000.0;
  const double accumulatedWh = fullRemainderWh_[index] + addedWh;
  const uint64_t wholeWh = (uint64_t)accumulatedWh;

  if (wholeWh) {
    const uint64_t room = (uint64_t)UINT32_MAX - fullWh_[index];
    const uint64_t accepted = wholeWh > room ? room : wholeWh;
    fullWh_[index] += (uint32_t)accepted;
    if (accepted == wholeWh) fullRemainderWh_[index] = accumulatedWh - (double)wholeWh;
    else fullRemainderWh_[index] = 0.0;
  } else {
    fullRemainderWh_[index] = accumulatedWh;
  }
}

bool EnergyCounter::detected(uint8_t sensorId) const {
  return sensorId != 0 && sensorId <= SENSOR_COUNT && detected_[(uint16_t)sensorId - 1U];
}

uint16_t EnergyCounter::detectedCount() const { return detectedCount_; }

uint32_t EnergyCounter::stringWh(uint8_t sensorId) const {
  return sensorId == 0 || sensorId > SENSOR_COUNT ? 0 : fullWh_[(uint16_t)sensorId - 1U];
}

uint64_t EnergyCounter::totalWh() const {
  uint64_t total = 0;
  for (uint16_t i = 0; i < SENSOR_COUNT; ++i) total += fullWh_[i];
  return total;
}

bool EnergyCounter::addStoredWh(uint8_t sensorId, uint32_t storedWh) {
  if (sensorId == 0 || sensorId > SENSOR_COUNT) return false;
  const uint16_t index = (uint16_t)sensorId - 1U;
  const uint64_t combined = (uint64_t)fullWh_[index] + storedWh;
  fullWh_[index] = combined > UINT32_MAX ? UINT32_MAX : (uint32_t)combined;
  if (storedWh != 0 && !detected_[index]) {
    detected_[index] = true;
    ++detectedCount_;
  }
  return true;
}

bool EnergyCounter::restoreStoredWh(uint8_t sensorId, uint32_t storedWh) {
  if (sensorId == 0 || sensorId > SENSOR_COUNT) return false;
  const uint16_t index = (uint16_t)sensorId - 1U;
  const uint64_t combined = (uint64_t)fullWh_[index] + storedWh;
  fullWh_[index] = combined > UINT32_MAX ? UINT32_MAX : (uint32_t)combined;
  if (!detected_[index]) {
    detected_[index] = true;
    ++detectedCount_;
  }
  return true;
}

bool EnergyCounter::resetString(uint8_t sensorId) {
  if (sensorId == 0 || sensorId > SENSOR_COUNT) return false;
  const uint16_t index = (uint16_t)sensorId - 1U;
  fullWh_[index] = 0;
  fullRemainderWh_[index] = 0.0;
  sampleSeen_[index] = false;
  lastSampleMs_[index] = 0;
  lastPowerW_[index] = 0.0f;
  return true;
}
