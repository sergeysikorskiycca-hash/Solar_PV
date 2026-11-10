/*
  Asynchronous, power-loss-safe A/B journals for the 24C32 at 0x57.
  Layout: bank A 0x000..0x3FF, bank B 0x400..0x7FF; 0x800..0xFFF reserved.
  Each bank has a 4-byte header and 255 little-endian uint32 Wh values.
*/
#include "EnergyEepromStore.h"
#include "EnergyEepromTransport.h"
#include <string.h>

EnergyEepromStore energyEepromStore;

uint16_t EnergyEepromStore::bankBase(uint8_t bank) {
  return bank == 0 ? 0U : BANK_SIZE;
}

uint8_t EnergyEepromStore::crc8Update(uint8_t crc, uint8_t value) {
  crc ^= value;
  for (uint8_t bit = 0; bit < 8; ++bit)
    crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  return crc;
}

uint8_t EnergyEepromStore::journalCrc(const uint8_t header[3],
                                     const uint8_t* data, uint16_t length) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < 3; ++i) crc = crc8Update(crc, header[i]);
  for (uint16_t i = 0; i < length; ++i) crc = crc8Update(crc, data[i]);
  return crc;
}

bool EnergyEepromStore::sequenceIsNewer(uint8_t candidate, uint8_t current) {
  const uint8_t difference = (uint8_t)(candidate - current);
  return difference != 0 && difference < 128;
}

void EnergyEepromStore::begin(uint32_t nowMs) {
  stage_ = SCAN_HEADER;
  pending_ = PENDING_NONE;
  waiting_ = false;
  ready_ = false;
  bestValid_ = false;
  saveRequested_ = false;
  lastSaveSucceeded_ = false;
  activeBank_ = -1;
  scanBank_ = 0;
  dataOffset_ = 0;
  nextActionMs_ = nowMs;
}

bool EnergyEepromStore::ready() const { return ready_; }
bool EnergyEepromStore::busy() const { return !ready_ || waiting_ || stage_ != READY; }
bool EnergyEepromStore::lastSaveSucceeded() const { return lastSaveSucceeded_; }

void EnergyEepromStore::requestSave() {
  saveRequested_ = true;
  if (ready_ && stage_ == READY) nextActionMs_ = 0;
}

void EnergyEepromStore::restartScan(uint32_t nowMs) {
  stage_ = SCAN_HEADER;
  pending_ = PENDING_NONE;
  waiting_ = false;
  ready_ = false;
  bestValid_ = false;
  activeBank_ = -1;
  scanBank_ = 0;
  dataOffset_ = 0;
  nextActionMs_ = nowMs + SCAN_RETRY_MS;
}

void EnergyEepromStore::advanceScanBank(uint32_t nowMs, EnergyCounter& counter) {
  ++scanBank_;
  dataOffset_ = 0;
  if (scanBank_ >= 2) {
    finishScan(nowMs, counter);
    return;
  }
  stage_ = SCAN_HEADER;
}

void EnergyEepromStore::finishScan(uint32_t nowMs, EnergyCounter& counter) {
  if (bestValid_) {
    for (uint16_t i = 0; i < EnergyCounter::SENSOR_COUNT; ++i) {
      const uint16_t offset = (uint16_t)(i * 4U);
      const uint32_t value = (uint32_t)bestData_[offset] |
        ((uint32_t)bestData_[offset + 1] << 8) |
        ((uint32_t)bestData_[offset + 2] << 16) |
        ((uint32_t)bestData_[offset + 3] << 24);
      counter.addStoredWh((uint8_t)(i + 1U), value);
    }
  }
  ready_ = true;
  stage_ = READY;
  nextActionMs_ = saveRequested_ ? nowMs : nowMs + SAVE_INTERVAL_MS;
}

void EnergyEepromStore::startSave(uint32_t nowMs, const EnergyCounter& counter) {
  for (uint16_t i = 0; i < EnergyCounter::SENSOR_COUNT; ++i) {
    const uint32_t value = counter.stringWh((uint8_t)(i + 1U));
    const uint16_t offset = (uint16_t)(i * 4U);
    bankData_[offset] = (uint8_t)value;
    bankData_[offset + 1] = (uint8_t)(value >> 8);
    bankData_[offset + 2] = (uint8_t)(value >> 16);
    bankData_[offset + 3] = (uint8_t)(value >> 24);
  }

  targetBank_ = activeBank_ == 0 ? 1 : 0;
  saveSequence_ = bestValid_ ? (uint8_t)(bestSequence_ + 1U) : 1U;
  saveHeader_[0] = 0xE7;
  saveHeader_[1] = 1;
  saveHeader_[2] = saveSequence_;
  saveHeader_[3] = journalCrc(saveHeader_, bankData_, DATA_SIZE);

  stage_ = SAVE_INVALIDATE;
  dataOffset_ = 0;
  pending_ = PENDING_NONE;
  saveRequested_ = false;
  lastSaveSucceeded_ = false;
  nextActionMs_ = nowMs;
}

void EnergyEepromStore::handleResponse(uint32_t nowMs, EnergyCounter& counter) {
  const EnergyEepromTransport::State transportState = energyEepromTransport.state();
  if (transportState == EnergyEepromTransport::QUEUED ||
      transportState == EnergyEepromTransport::WAITING) return;

  if (transportState == EnergyEepromTransport::FAILED) {
    energyEepromTransport.consume();
    waiting_ = false;
    pending_ = PENDING_NONE;
    if (stage_ == SCAN_HEADER || stage_ == SCAN_DATA) {
      restartScan(nowMs);
    } else {
      stage_ = READY;
      saveRequested_ = true;
      nextActionMs_ = nowMs + SAVE_RETRY_MS;
    }
    return;
  }

  if (transportState != EnergyEepromTransport::COMPLETE) return;

  const uint8_t* result = energyEepromTransport.resultData();
  const uint8_t resultLength = energyEepromTransport.resultLength();
  const Pending completed = pending_;
  energyEepromTransport.consume();
  waiting_ = false;
  pending_ = PENDING_NONE;

  if (completed == PENDING_READ_HEADER) {
    if (resultLength != 4 || result[0] != 0xE7 || result[1] != 1) {
      advanceScanBank(nowMs, counter);
      return;
    }
    memcpy(header_, result, 4);
    dataOffset_ = 0;
    stage_ = SCAN_DATA;
    return;
  }

  if (completed == PENDING_READ_DATA) {
    if (resultLength != pendingLength_ || dataOffset_ + resultLength > DATA_SIZE) {
      restartScan(nowMs);
      return;
    }
    memcpy(bankData_ + dataOffset_, result, resultLength);
    dataOffset_ = (uint16_t)(dataOffset_ + resultLength);
    if (dataOffset_ >= DATA_SIZE) {
      const uint8_t crc = journalCrc(header_, bankData_, DATA_SIZE);
      if (crc == header_[3] &&
          (!bestValid_ || sequenceIsNewer(header_[2], bestSequence_))) {
        memcpy(bestData_, bankData_, DATA_SIZE);
        bestSequence_ = header_[2];
        bestValid_ = true;
        activeBank_ = (int8_t)scanBank_;
      }
      advanceScanBank(nowMs, counter);
    }
    return;
  }

  if (completed == PENDING_INVALIDATE) {
    stage_ = SAVE_DATA;
    dataOffset_ = 0;
    return;
  }

  if (completed == PENDING_WRITE_DATA) {
    dataOffset_ = (uint16_t)(dataOffset_ + pendingLength_);
    if (dataOffset_ >= DATA_SIZE) stage_ = SAVE_HEADER;
    return;
  }

  if (completed == PENDING_WRITE_HEADER) {
    activeBank_ = (int8_t)targetBank_;
    bestSequence_ = saveSequence_;
    bestValid_ = true;
    lastSaveSucceeded_ = true;
    stage_ = READY;
    nextActionMs_ = saveRequested_ ? nowMs : nowMs + SAVE_INTERVAL_MS;
  }
}

void EnergyEepromStore::schedule(uint32_t nowMs, EnergyCounter& counter) {
  if (waiting_ || (int32_t)(nowMs - nextActionMs_) < 0) return;

  if (stage_ == SCAN_HEADER) {
    if (energyEepromTransport.queueRead(bankBase(scanBank_), 4)) {
      pending_ = PENDING_READ_HEADER;
      waiting_ = true;
    }
    return;
  }

  if (stage_ == SCAN_DATA) {
    const uint8_t length = (uint8_t)((DATA_SIZE - dataOffset_) > 8 ? 8 : (DATA_SIZE - dataOffset_));
    if (length && energyEepromTransport.queueRead((uint16_t)(bankBase(scanBank_) + 4U + dataOffset_), length)) {
      pendingLength_ = length;
      pending_ = PENDING_READ_DATA;
      waiting_ = true;
    }
    return;
  }

  if (stage_ == READY) {
    if (saveRequested_ || (int32_t)(nowMs - nextActionMs_) >= 0) startSave(nowMs, counter);
    return;
  }

  if (stage_ == SAVE_INVALIDATE) {
    const uint8_t invalid = 0;
    if (energyEepromTransport.queueWrite(bankBase(targetBank_), &invalid, 1)) {
      pending_ = PENDING_INVALIDATE;
      waiting_ = true;
    }
    return;
  }

  if (stage_ == SAVE_DATA) {
    const uint16_t address = (uint16_t)(bankBase(targetBank_) + 4U + dataOffset_);
    const uint8_t pageRoom = (uint8_t)(32U - (address & 0x1FU));
    uint8_t length = (uint8_t)((DATA_SIZE - dataOffset_) > 8 ? 8 : (DATA_SIZE - dataOffset_));
    if (length > pageRoom) length = pageRoom;
    if (length && energyEepromTransport.queueWrite(address, bankData_ + dataOffset_, length)) {
      pendingLength_ = length;
      pending_ = PENDING_WRITE_DATA;
      waiting_ = true;
    }
    return;
  }

  if (stage_ == SAVE_HEADER) {
    if (energyEepromTransport.queueWrite(bankBase(targetBank_), saveHeader_, 4)) {
      pending_ = PENDING_WRITE_HEADER;
      waiting_ = true;
    }
  }
}

void EnergyEepromStore::poll(uint32_t nowMs, EnergyCounter& counter) {
  if (waiting_) handleResponse(nowMs, counter);
  if (waiting_) return;
  schedule(nowMs, counter);
}
