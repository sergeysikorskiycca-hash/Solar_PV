/*
  Non-blocking calendar history for the 24C32 range 0x800..0xFFF.
  Two 512-byte A/B checkpoints protect the current day; the remaining
  1024 bytes form a variable-length ring of finalized daily records.
*/
#include "EnergyCalendarStore.h"
#include "EnergyEepromTransport.h"
#include "EnergyEepromStore.h"
#include <math.h>
#include <string.h>

EnergyCalendarStore energyCalendarStore;

bool EnergyCalendarStore::validDate(uint16_t year, uint8_t month, uint8_t day) {
  if (year < 2000 || year > 2199 || month < 1 || month > 12 || day < 1) return false;
  static const uint8_t days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  uint8_t limit = days[month - 1];
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  if (month == 2 && leap) ++limit;
  return day <= limit;
}

uint32_t EnergyCalendarStore::makeDate(uint16_t year, uint8_t month, uint8_t day) {
  return (uint32_t)year * 10000UL + (uint32_t)month * 100UL + day;
}

uint8_t EnergyCalendarStore::crc8(const uint8_t* data, uint16_t length) {
  uint8_t crc = 0;
  for (uint16_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}

bool EnergyCalendarStore::newer(uint8_t a, uint8_t b) {
  const uint8_t difference = (uint8_t)(a - b);
  return difference != 0 && difference < 128;
}

uint16_t EnergyCalendarStore::encodeValues(uint8_t* out, const uint8_t* ids,
                                            const uint32_t* values, uint8_t count) {
  uint16_t offset = 0;
  for (uint16_t i = 0; i < count; ++i) {
    if (!ids[i]) continue;
    out[offset++] = ids[i];
    uint32_t value = values[i];
    if (value > 0x00FFFFFEUL) value = 0x00FFFFFEUL;
    out[offset++] = (uint8_t)value;
    out[offset++] = (uint8_t)(value >> 8);
    out[offset++] = (uint8_t)(value >> 16);
  }
  return offset;
}

bool EnergyCalendarStore::decodeValues(const uint8_t* data, uint16_t length,
                                        uint8_t count, uint8_t* ids, uint32_t* values,
                                        uint16_t& used) {
  used = 0;
  if ((uint32_t)count * 4U > length) return false;
  for (uint16_t i = 0; i < count; ++i) {
    const uint8_t id = data[used++];
    if (!id) return false;
    const uint32_t value = (uint32_t)data[used] |
      ((uint32_t)data[used + 1] << 8) | ((uint32_t)data[used + 2] << 16);
    used += 3;
    ids[i] = id;
    values[i] = value;
  }
  return true;
}

void EnergyCalendarStore::begin(uint32_t nowMs) {
  stage_ = SCAN_CP0;
  pending_ = PENDING_NONE;
  waiting_ = false;
  ready_ = false;
  checkpointValid_ = false;
  restored_ = false;
  saveRequested_ = false;
  archiveRequested_ = false;
  storageLimited_ = false;
  scanBank_ = 0;
  activeCheckpoint_ = 0;
  checkpointSequence_ = 0;
  archiveSequence_ = 0;
  ioOffset_ = 0;
  checkpointLength_ = 0;
  archiveLength_ = 0;
  archiveAppendOffset_ = 0;
  currentDate_ = 0;
  checkpointDate_ = 0;
  lastCheckpointMs_ = nowMs;
  nextActionMs_ = nowMs;
  detectedCount_ = 0;
  checkpointCount_ = 0;
  pendingCount_ = 0;
  archiveCount_ = 0;
  memset(dailyWh_, 0, sizeof(dailyWh_));
  memset(dailyRemainderWh_, 0, sizeof(dailyRemainderWh_));
  memset(sampleSeen_, 0, sizeof(sampleSeen_));
  memset(checkpointWh_, 0, sizeof(checkpointWh_));
  memset(checkpointIds_, 0, sizeof(checkpointIds_));
  memset(pendingWh_, 0, sizeof(pendingWh_));
  memset(pendingIds_, 0, sizeof(pendingIds_));
  memset(archiveBuffer_, 0, sizeof(archiveBuffer_));
  memset(archiveIndex_, 0, sizeof(archiveIndex_));
}

bool EnergyCalendarStore::ready() const { return ready_; }
uint32_t EnergyCalendarStore::currentDate() const { return currentDate_; }
bool EnergyCalendarStore::storageLimited() const { return storageLimited_; }

void EnergyCalendarStore::registerId(uint8_t id) {
  if (!id) return;
  for (uint16_t i = 0; i < detectedCount_; ++i)
    if (detectedIds_[i] == id) return;
  if (detectedCount_ < 255) detectedIds_[detectedCount_++] = id;
}

uint32_t EnergyCalendarStore::currentDayWh(uint8_t sensorId) const {
  return sensorId ? dailyWh_[(uint16_t)sensorId - 1U] : 0;
}

uint8_t EnergyCalendarStore::archivedDayCount() const { return archiveCount_; }

uint32_t EnergyCalendarStore::archivedDate(uint8_t index) const {
  return index < archiveCount_ ? archiveIndex_[index].date : 0;
}

uint32_t EnergyCalendarStore::archivedDayWh(uint8_t index, uint8_t sensorId) const {
  if (index >= archiveCount_ || !sensorId) return 0;
  const ArchiveIndex& item = archiveIndex_[index];
  if (!item.valid || item.offset + item.length > ARCHIVE_SIZE) return 0;
  const uint8_t count = archiveBuffer_[item.offset + 7];
  uint16_t offset = (uint16_t)(item.offset + 8U);
  for (uint16_t i = 0; i < count; ++i) {
    if (offset + 4U > item.offset + item.length - 1U) return 0;
    const uint8_t id = archiveBuffer_[offset++];
    const uint32_t value = (uint32_t)archiveBuffer_[offset] |
      ((uint32_t)archiveBuffer_[offset + 1] << 8) |
      ((uint32_t)archiveBuffer_[offset + 2] << 16);
    offset += 3;
    if (id == sensorId) return value;
  }
  return 0;
}

void EnergyCalendarStore::snapshotPendingDay(uint32_t date) {
  pendingDate_ = date;
  pendingCount_ = 0;
  for (uint16_t i = 0; i < detectedCount_; ++i) {
    const uint8_t id = detectedIds_[i];
    const uint32_t value = dailyWh_[(uint16_t)id - 1U];
    if (!value) continue;
    pendingIds_[pendingCount_] = id;
    pendingWh_[pendingCount_] = value;
    ++pendingCount_;
  }
  archiveRequested_ = pendingCount_ != 0 && date != 0;
}

void EnergyCalendarStore::applyRestoredCheckpoint() {
  if (restored_ || !currentDate_ || !ready_) return;
  restored_ = true;
  if (!checkpointValid_) return;

  if (checkpointDate_ == currentDate_) {
    for (uint16_t i = 0; i < checkpointCount_; ++i) {
      const uint8_t id = checkpointIds_[i];
      registerId(id);
      const uint16_t index = (uint16_t)id - 1U;
      const uint64_t total = (uint64_t)dailyWh_[index] + checkpointWh_[i];
      dailyWh_[index] = total > 0x00FFFFFEUL ? 0x00FFFFFEUL : (uint32_t)total;
    }
  } else {
    pendingDate_ = checkpointDate_;
    pendingCount_ = checkpointCount_;
    for (uint16_t i = 0; i < checkpointCount_; ++i) {
      pendingIds_[i] = checkpointIds_[i];
      pendingWh_[i] = checkpointWh_[i];
    }
    archiveRequested_ = pendingCount_ != 0;
  }
  saveRequested_ = true;
}

void EnergyCalendarStore::update(uint8_t sensorId, float powerW, uint32_t nowMs,
                                  bool timeValid, uint16_t year, uint8_t month, uint8_t day) {
  if (!sensorId || !isfinite(powerW)) return;
  const uint16_t index = (uint16_t)sensorId - 1U;
  if (!timeValid || !validDate(year, month, day)) {
    sampleSeen_[index] = false;
    return;
  }

  const uint32_t date = makeDate(year, month, day);
  if (!currentDate_) currentDate_ = date;
  else if (date != currentDate_) {
    snapshotPendingDay(currentDate_);
    currentDate_ = date;
    memset(dailyWh_, 0, sizeof(dailyWh_));
    memset(dailyRemainderWh_, 0, sizeof(dailyRemainderWh_));
    memset(sampleSeen_, 0, sizeof(sampleSeen_));
    restored_ = false;
    saveRequested_ = true;
  }

  registerId(sensorId);
  if (powerW < 0.0f) powerW = 0.0f;

  if (sampleSeen_[index]) {
    const uint32_t elapsed = nowMs - lastSampleMs_[index];
    if (elapsed && elapsed <= 3000UL && lastPowerW_[index] > 0.0f) {
      const double added = (double)lastPowerW_[index] * (double)elapsed / 3600000.0;
      const double total = dailyRemainderWh_[index] + added;
      const uint32_t whole = (uint32_t)total;
      const uint32_t room = 0x00FFFFFEUL - dailyWh_[index];
      const uint32_t accepted = whole > room ? room : whole;
      dailyWh_[index] += accepted;
      dailyRemainderWh_[index] = accepted == whole ? total - whole : 0.0;
    }
  }
  sampleSeen_[index] = true;
  lastSampleMs_[index] = nowMs;
  lastPowerW_[index] = powerW;
  applyRestoredCheckpoint();
}

bool EnergyCalendarStore::queueRead(uint16_t address, uint8_t length) {
  return energyEepromTransport.queueRead(address, length);
}

bool EnergyCalendarStore::queueWrite(uint16_t address, const uint8_t* data, uint8_t length) {
  return energyEepromTransport.queueWrite(address, data, length);
}

void EnergyCalendarStore::scanArchive() {
  archiveCount_ = 0;
  bool haveNewest = false;
  uint8_t newestSequence = 0;
  uint16_t newestEnd = 0;

  for (uint16_t offset = 0; offset + 9U <= ARCHIVE_SIZE; ++offset) {
    const uint8_t* p = archiveBuffer_ + offset;
    if (p[0] != 0xD6 || p[1] != 1) continue;
    const uint8_t count = p[7];
    const uint16_t length = (uint16_t)(9U + (uint16_t)count * 4U);
    if (length > ARCHIVE_SIZE || offset + length > ARCHIVE_SIZE) continue;
    if (crc8(p, length - 1U) != p[length - 1U]) continue;

    uint16_t check = 0;
    uint8_t ids[255];
    uint32_t values[255];
    if (!decodeValues(p + 8, (uint16_t)(length - 9U), count, ids, values, check) ||
        check != (uint16_t)(length - 9U)) continue;

    const uint32_t recordDate = (uint32_t)p[3] | ((uint32_t)p[4] << 8) |
                                ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 24);
    int16_t duplicate = -1;
    for (uint8_t n = 0; n < archiveCount_; ++n)
      if (archiveIndex_[n].date == recordDate) { duplicate = (int16_t)n; break; }
    if (duplicate >= 0) {
      if (newer(p[2], archiveIndex_[duplicate].sequence)) {
        ArchiveIndex& item = archiveIndex_[duplicate];
        item.offset = offset;
        item.length = length;
        item.sequence = p[2];
        item.valid = true;
      }
    } else if (archiveCount_ < MAX_ARCHIVED_DAYS) {
      ArchiveIndex& item = archiveIndex_[archiveCount_++];
      item.offset = offset;
      item.length = length;
      item.date = recordDate;
      item.sequence = p[2];
      item.valid = true;
    }

    if (!haveNewest || newer(p[2], newestSequence)) {
      haveNewest = true;
      newestSequence = p[2];
      newestEnd = (uint16_t)(offset + length);
    }
  }

  if (haveNewest) {
    archiveSequence_ = newestSequence;
    archiveAppendOffset_ = newestEnd >= ARCHIVE_SIZE ? 0 : newestEnd;
  } else {
    archiveSequence_ = 0;
    archiveAppendOffset_ = 0;
  }

  // Keep records in chronological sequence order, oldest first.
  for (uint8_t i = 0; i < archiveCount_; ++i) {
    for (uint8_t j = (uint8_t)(i + 1); j < archiveCount_; ++j) {
      if (newer(archiveIndex_[i].sequence, archiveIndex_[j].sequence)) {
        ArchiveIndex temp = archiveIndex_[i];
        archiveIndex_[i] = archiveIndex_[j];
        archiveIndex_[j] = temp;
      }
    }
  }
}

void EnergyCalendarStore::startCheckpoint(uint32_t nowMs) {
  uint8_t ids[255];
  uint32_t values[255];
  uint8_t count = 0;
  for (uint16_t i = 0; i < detectedCount_; ++i) {
    const uint8_t id = detectedIds_[i];
    if (!dailyWh_[(uint16_t)id - 1U]) continue;
    ids[count] = id;
    values[count] = dailyWh_[(uint16_t)id - 1U];
    ++count;
  }

  const uint16_t dataLength = (uint16_t)count * 4U;
  checkpointLength_ = (uint16_t)(9U + dataLength);
  if (checkpointLength_ > CHECKPOINT_SIZE) {
    storageLimited_ = true;
    saveRequested_ = false;
    lastCheckpointMs_ = nowMs;
    return;
  }

  memset(checkpointBuffer_, 0, sizeof(checkpointBuffer_));
  checkpointBuffer_[0] = 0xC7;
  checkpointBuffer_[1] = 1;
  checkpointBuffer_[2] = (uint8_t)(checkpointSequence_ + 1U);
  checkpointBuffer_[3] = (uint8_t)currentDate_;
  checkpointBuffer_[4] = (uint8_t)(currentDate_ >> 8);
  checkpointBuffer_[5] = (uint8_t)(currentDate_ >> 16);
  checkpointBuffer_[6] = (uint8_t)(currentDate_ >> 24);
  checkpointBuffer_[7] = count;
  encodeValues(checkpointBuffer_ + 8, ids, values, count);
  checkpointBuffer_[checkpointLength_ - 1U] = crc8(checkpointBuffer_, checkpointLength_ - 1U);

  targetBank_ = activeCheckpoint_ == 0 ? 1 : 0;
  ioOffset_ = 1;
  stage_ = CP_INVALIDATE;
  saveRequested_ = false;
  lastCheckpointMs_ = nowMs;
}

void EnergyCalendarStore::startArchive(uint32_t nowMs) {
  const uint16_t dataLength = (uint16_t)pendingCount_ * 4U;
  archiveLength_ = (uint16_t)(9U + dataLength);
  if (!pendingCount_ || archiveLength_ > ARCHIVE_SIZE) {
    storageLimited_ = true;
    archiveRequested_ = false;
    return;
  }

  if (archiveAppendOffset_ + archiveLength_ > ARCHIVE_SIZE) archiveAppendOffset_ = 0;
  archiveWriteOffset_ = archiveAppendOffset_;
  memset(recordBuffer_, 0, sizeof(recordBuffer_));
  recordBuffer_[0] = 0xD6;
  recordBuffer_[1] = 1;
  recordBuffer_[2] = (uint8_t)(archiveSequence_ + 1U);
  recordBuffer_[3] = (uint8_t)pendingDate_;
  recordBuffer_[4] = (uint8_t)(pendingDate_ >> 8);
  recordBuffer_[5] = (uint8_t)(pendingDate_ >> 16);
  recordBuffer_[6] = (uint8_t)(pendingDate_ >> 24);
  recordBuffer_[7] = pendingCount_;
  encodeValues(recordBuffer_ + 8, pendingIds_, pendingWh_, pendingCount_);
  recordBuffer_[archiveLength_ - 1U] = crc8(recordBuffer_, archiveLength_ - 1U);

  ioOffset_ = 1;
  stage_ = ARCH_INVALIDATE;
  archiveRequested_ = false;
  (void)nowMs;
}

void EnergyCalendarStore::handleResponse(uint32_t nowMs) {
  const EnergyEepromTransport::State state = energyEepromTransport.state();
  if (state == EnergyEepromTransport::QUEUED || state == EnergyEepromTransport::WAITING) return;
  if (state == EnergyEepromTransport::FAILED) {
    energyEepromTransport.consume();
    waiting_ = false;
    pending_ = PENDING_NONE;
    if (stage_ == SCAN_CP0 || stage_ == SCAN_CP1 || stage_ == SCAN_ARCHIVE) {
      stage_ = SCAN_CP0;
      scanBank_ = 0;
      ioOffset_ = 0;
      checkpointValid_ = false;
      checkpointSequence_ = 0;
      checkpointCount_ = 0;
      archiveCount_ = 0;
      nextActionMs_ = nowMs + RETRY_MS;
    } else {
      stage_ = READY;
      saveRequested_ = true;
      archiveRequested_ = pendingCount_ != 0;
      nextActionMs_ = nowMs + RETRY_MS;
    }
    return;
  }
  if (state != EnergyEepromTransport::COMPLETE) return;

  const uint8_t* data = energyEepromTransport.resultData();
  const uint8_t length = energyEepromTransport.resultLength();
  const Pending completed = pending_;
  energyEepromTransport.consume();
  waiting_ = false;
  pending_ = NONE;

  if (completed == PENDING_READ) {
    if (stage_ == SCAN_CP0 || stage_ == SCAN_CP1) {
      memcpy(checkpointBuffer_ + ioOffset_, data, length);
      ioOffset_ = (uint16_t)(ioOffset_ + length);
      if (ioOffset_ >= CHECKPOINT_SIZE) {
        const uint8_t* p = checkpointBuffer_;
        if (p[0] == 0xC7 && p[1] == 1) {
          const uint8_t count = p[7];
          const uint16_t recordLength = (uint16_t)(9U + (uint16_t)count * 4U);
          if (recordLength <= CHECKPOINT_SIZE &&
              crc8(p, recordLength - 1U) == p[recordLength - 1U]) {
            const uint8_t sequence = p[2];
            if (!checkpointValid_ || newer(sequence, checkpointSequence_)) {
              uint16_t used = 0;
              if (decodeValues(p + 8, (uint16_t)(recordLength - 9U), count,
                               checkpointIds_, checkpointWh_, used) &&
                  used == (uint16_t)(recordLength - 9U)) {
                checkpointValid_ = true;
                checkpointSequence_ = sequence;
                checkpointDate_ = (uint32_t)p[3] | ((uint32_t)p[4] << 8) |
                                  ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 24);
                checkpointCount_ = count;
                activeCheckpoint_ = scanBank_;
              }
            }
          }
        }
        if (scanBank_ == 0) {
          scanBank_ = 1;
          ioOffset_ = 0;
          memset(checkpointBuffer_, 0, sizeof(checkpointBuffer_));
          stage_ = SCAN_CP1;
        } else {
          stage_ = SCAN_ARCHIVE;
          ioOffset_ = 0;
        }
      }
    } else if (stage_ == SCAN_ARCHIVE) {
      memcpy(archiveBuffer_ + ioOffset_, data, length);
      ioOffset_ = (uint16_t)(ioOffset_ + length);
      if (ioOffset_ >= ARCHIVE_SIZE) {
        scanArchive();
        ready_ = true;
        stage_ = READY;
        applyRestoredCheckpoint();
        nextActionMs_ = nowMs + CHECKPOINT_INTERVAL_MS;
      }
    }
    return;
  }

  if (completed == PENDING_WRITE) {
    if (stage_ == CP_INVALIDATE) {
      stage_ = CP_BODY;
      ioOffset_ = 1;
    } else if (stage_ == CP_BODY) {
      ioOffset_ = (uint16_t)(ioOffset_ + pendingLength_);
      if (ioOffset_ >= checkpointLength_) stage_ = CP_COMMIT;
    } else if (stage_ == CP_COMMIT) {
      activeCheckpoint_ = targetBank_;
      checkpointSequence_ = checkpointBuffer_[2];
      checkpointDate_ = currentDate_;
      checkpointValid_ = true;
      checkpointCount_ = checkpointBuffer_[7];
      uint16_t used = 0;
      decodeValues(checkpointBuffer_ + 8, (uint16_t)(checkpointLength_ - 9U),
                   checkpointCount_, checkpointIds_, checkpointWh_, used);
      stage_ = READY;
      nextActionMs_ = nowMs + CHECKPOINT_INTERVAL_MS;
    } else if (stage_ == ARCH_INVALIDATE) {
      stage_ = ARCH_BODY;
      ioOffset_ = 1;
    } else if (stage_ == ARCH_BODY) {
      ioOffset_ = (uint16_t)(ioOffset_ + pendingLength_);
      if (ioOffset_ >= archiveLength_) stage_ = ARCH_COMMIT;
    } else if (stage_ == ARCH_COMMIT) {
      archiveBuffer_[archiveWriteOffset_] = 0xD6;
      memcpy(archiveBuffer_ + archiveWriteOffset_ + 1, recordBuffer_ + 1, archiveLength_ - 1U);
      archiveSequence_ = recordBuffer_[2];
      archiveAppendOffset_ = (uint16_t)(archiveWriteOffset_ + archiveLength_);
      if (archiveAppendOffset_ >= ARCHIVE_SIZE) archiveAppendOffset_ = 0;
      scanArchive();
      pendingCount_ = 0;
      stage_ = READY;
      saveRequested_ = true;
    }
  }
}

void EnergyCalendarStore::schedule(uint32_t nowMs) {
  if (waiting_ || (int32_t)(nowMs - nextActionMs_) < 0) return;
  if (stage_ == SCAN_CP0 || stage_ == SCAN_CP1) {
    const uint16_t base = (uint16_t)(BASE + (uint16_t)scanBank_ * CHECKPOINT_SIZE);
    const uint8_t length = (uint8_t)((CHECKPOINT_SIZE - ioOffset_) > 8U ? 8U : (CHECKPOINT_SIZE - ioOffset_));
    if (length && queueRead((uint16_t)(base + ioOffset_), length)) {
      pending_ = PENDING_READ; pendingLength_ = length; waiting_ = true;
    }
    return;
  }
  if (stage_ == SCAN_ARCHIVE) {
    const uint8_t length = (uint8_t)((ARCHIVE_SIZE - ioOffset_) > 8U ? 8U : (ARCHIVE_SIZE - ioOffset_));
    if (length && queueRead((uint16_t)(ARCHIVE_BASE + ioOffset_), length)) {
      pending_ = READ; pendingLength_ = length; waiting_ = true;
    }
    return;
  }
  if (stage_ == READY) {
    if (archiveRequested_) { startArchive(nowMs); return; }
    if (saveRequested_ || (currentDate_ && nowMs - lastCheckpointMs_ >= CHECKPOINT_INTERVAL_MS)) {
      startCheckpoint(nowMs);
      return;
    }
    return;
  }

  if (stage_ == CP_INVALIDATE || stage_ == ARCH_INVALIDATE) {
    const uint8_t invalid = 0;
    const uint16_t base = stage_ == CP_INVALIDATE
      ? (uint16_t)(BASE + (uint16_t)targetBank_ * CHECKPOINT_SIZE)
      : (uint16_t)(ARCHIVE_BASE + archiveWriteOffset_);
    if (queueWrite(base, &invalid, 1)) { pending_ = PENDING_WRITE; waiting_ = true; }
    return;
  }

  if (stage_ == CP_BODY || stage_ == ARCH_BODY) {
    const bool checkpoint = stage_ == CP_BODY;
    const uint16_t base = checkpoint
      ? (uint16_t)(BASE + (uint16_t)targetBank_ * CHECKPOINT_SIZE)
      : (uint16_t)(ARCHIVE_BASE + archiveWriteOffset_);
    const uint16_t total = checkpoint ? checkpointLength_ : archiveLength_;
    const uint8_t* source = checkpoint ? checkpointBuffer_ : recordBuffer_;
    const uint16_t address = (uint16_t)(base + ioOffset_);
    const uint8_t pageRoom = (uint8_t)(32U - (address & 0x1FU));
    uint8_t length = (uint8_t)((total - ioOffset_) > 8U ? 8U : (total - ioOffset_));
    if (length > pageRoom) length = pageRoom;
    if (length && queueWrite(address, source + ioOffset_, length)) {
      pending_ = WRITE; pendingLength_ = length; waiting_ = true;
    }
    return;
  }

  if (stage_ == CP_COMMIT || stage_ == ARCH_COMMIT) {
    const uint8_t magic = stage_ == CP_COMMIT ? 0xC7 : 0xD6;
    const uint16_t base = stage_ == CP_COMMIT
      ? (uint16_t)(BASE + (uint16_t)targetBank_ * CHECKPOINT_SIZE)
      : (uint16_t)(ARCHIVE_BASE + archiveWriteOffset_);
    if (queueWrite(base, &magic, 1)) { pending_ = WRITE; waiting_ = true; }
  }
}

void EnergyCalendarStore::poll(uint32_t nowMs) {
  if (waiting_) {
    handleResponse(nowMs);
    if (waiting_) return;
  }
  if (!ready_) {
    if (!energyEepromStore.busy() &&
        energyEepromTransport.state() == EnergyEepromTransport::IDLE)
      schedule(nowMs);
    return;
  }
  if (!energyEepromStore.busy() &&
      energyEepromTransport.state() == EnergyEepromTransport::IDLE)
    schedule(nowMs);
}
