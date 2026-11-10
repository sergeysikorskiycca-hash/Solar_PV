/*
  Crash-safe A/B persistence for 255 per-string Wh counters in the DS3231
  module's 24C32 EEPROM. Each journal occupies exactly 1024 bytes.
*/
#pragma once
#include <Arduino.h>
#include "EnergyCounter.h"

class EnergyEepromStore {
public:
  void begin(uint32_t nowMs);
  void poll(uint32_t nowMs, EnergyCounter& counter);
  bool ready() const;
  bool busy() const;
  bool lastSaveSucceeded() const;
  void requestSave();

private:
  static constexpr uint16_t BANK_SIZE = 1024;
  static constexpr uint16_t DATA_SIZE = 1020;
  static constexpr uint32_t SAVE_INTERVAL_MS = 3600000UL;
  static constexpr uint32_t SCAN_RETRY_MS = 30000UL;
  static constexpr uint32_t SAVE_RETRY_MS = 60000UL;

  enum Stage : uint8_t { SCAN_HEADER, SCAN_DATA, READY, SAVE_INVALIDATE, SAVE_DATA, SAVE_HEADER };
  enum Pending : uint8_t { PENDING_NONE, PENDING_READ_HEADER, PENDING_READ_DATA,
                           PENDING_INVALIDATE, PENDING_WRITE_DATA, PENDING_WRITE_HEADER };

  Stage stage_ = SCAN_HEADER;
  Pending pending_ = PENDING_NONE;
  bool waiting_ = false;
  bool ready_ = false;
  bool bestValid_ = false;
  bool saveRequested_ = false;
  bool retryDelayActive_ = false;
  bool lastSaveSucceeded_ = false;
  int8_t activeBank_ = -1;
  uint8_t scanBank_ = 0;
  uint8_t bestSequence_ = 0;
  uint8_t saveSequence_ = 0;
  uint8_t targetBank_ = 0;
  uint8_t header_[4] = {};
  uint8_t saveHeader_[4] = {};
  uint8_t bankData_[DATA_SIZE] = {};
  uint8_t bestData_[DATA_SIZE] = {};
  uint16_t dataOffset_ = 0;
  uint8_t pendingLength_ = 0;
  uint32_t nextActionMs_ = 0;

  static uint16_t bankBase(uint8_t bank);
  static uint8_t crc8Update(uint8_t crc, uint8_t value);
  static uint8_t journalCrc(const uint8_t header[3], const uint8_t* data, uint16_t length);
  static bool sequenceIsNewer(uint8_t candidate, uint8_t current);
  void schedule(uint32_t nowMs, EnergyCounter& counter);
  void handleResponse(uint32_t nowMs, EnergyCounter& counter);
  void advanceScanBank(uint32_t nowMs, EnergyCounter& counter);
  void finishScan(uint32_t nowMs, EnergyCounter& counter);
  void restartScan(uint32_t nowMs);
  void startSave(uint32_t nowMs, const EnergyCounter& counter);
};

extern EnergyEepromStore energyEepromStore;
