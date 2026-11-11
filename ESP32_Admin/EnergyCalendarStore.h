/*
  Calendar energy history stored in the reserved 24C32 range 0x800..0xFFF.
  Only sensor IDs that have actually reported data are stored.
*/
#pragma once
#include <Arduino.h>

class EnergyCalendarStore {
public:
  void begin(uint32_t nowMs);
  void update(uint8_t sensorId, float powerW, uint32_t nowMs,
              bool timeValid, uint16_t year, uint8_t month, uint8_t day);
  void poll(uint32_t nowMs);
  bool ready() const;
  uint32_t currentDate() const;
  uint32_t currentDayWh(uint8_t sensorId) const;
  uint8_t archivedDayCount() const;
  uint32_t archivedDate(uint8_t index) const;
  uint32_t archivedDayWh(uint8_t index, uint8_t sensorId) const;
  bool storageLimited() const;

private:
  static constexpr uint16_t BASE = 0x0800;
  static constexpr uint16_t REGION_SIZE = 2048;
  static constexpr uint16_t CHECKPOINT_SIZE = 512;
  static constexpr uint16_t ARCHIVE_BASE = BASE + 2U * CHECKPOINT_SIZE;
  static constexpr uint16_t ARCHIVE_SIZE = REGION_SIZE - 2U * CHECKPOINT_SIZE;
  static constexpr uint32_t CHECKPOINT_INTERVAL_MS = 3600000UL;
  static constexpr uint32_t RETRY_MS = 60000UL;
  static constexpr uint8_t MAX_ARCHIVED_DAYS = 100;

  struct ArchiveIndex {
    uint16_t offset;
    uint16_t length;
    uint32_t date;
    uint8_t sequence;
    bool valid;
  };

  enum Stage : uint8_t {
    SCAN_CP0, SCAN_CP1, SCAN_ARCHIVE, READY,
    CP_INVALIDATE, CP_BODY, CP_COMMIT,
    ARCH_INVALIDATE, ARCH_BODY, ARCH_COMMIT
  };
  enum Pending : uint8_t { PENDING_NONE, PENDING_READ, PENDING_WRITE };

  Stage stage_ = SCAN_CP0;
  Pending pending_ = PENDING_NONE;
  bool waiting_ = false;
  bool ready_ = false;
  bool checkpointValid_ = false;
  bool restored_ = false;
  bool saveRequested_ = false;
  bool archiveRequested_ = false;
  bool storageLimited_ = false;
  uint8_t scanBank_ = 0;
  uint8_t activeCheckpoint_ = 0;
  uint8_t checkpointSequence_ = 0;
  uint8_t archiveSequence_ = 0;
  uint8_t targetBank_ = 0;
  uint8_t pendingLength_ = 0;
  uint16_t ioOffset_ = 0;
  uint16_t checkpointLength_ = 0;
  uint16_t archiveLength_ = 0;
  uint16_t archiveWriteOffset_ = 0;
  uint16_t archiveAppendOffset_ = 0;
  uint32_t currentDate_ = 0;
  uint32_t checkpointDate_ = 0;
  uint32_t lastCheckpointMs_ = 0;
  uint32_t nextActionMs_ = 0;
  uint8_t detectedCount_ = 0;
  uint8_t detectedIds_[2] = {};
  uint32_t dailyWh_[2] = {};
  double dailyRemainderWh_[2] = {};
  float lastPowerW_[2] = {};
  uint32_t lastSampleMs_[2] = {};
  bool sampleSeen_[2] = {};
  uint32_t checkpointWh_[2] = {};
  uint8_t checkpointIds_[2] = {};
  uint8_t checkpointCount_ = 0;
  uint32_t pendingDate_ = 0;
  uint32_t pendingWh_[2] = {};
  uint8_t pendingIds_[2] = {};
  uint8_t pendingCount_ = 0;
  uint8_t checkpointBuffer_[CHECKPOINT_SIZE] = {};
  uint8_t archiveBuffer_[ARCHIVE_SIZE] = {};
  uint8_t recordBuffer_[ARCHIVE_SIZE] = {};
  ArchiveIndex archiveIndex_[MAX_ARCHIVED_DAYS] = {};
  uint8_t archiveCount_ = 0;

  static bool validDate(uint16_t year, uint8_t month, uint8_t day);
  static uint32_t makeDate(uint16_t year, uint8_t month, uint8_t day);
  static uint8_t crc8(const uint8_t* data, uint16_t length);
  static bool newer(uint8_t a, uint8_t b);
  static uint16_t encodeValues(uint8_t* out, const uint8_t* ids,
                               const uint32_t* values, uint8_t count);
  static bool decodeValues(const uint8_t* data, uint16_t length,
                           uint8_t count, uint8_t* ids, uint32_t* values,
                           uint16_t& used);
  void registerId(uint8_t id);
  void snapshotPendingDay(uint32_t date);
  void applyRestoredCheckpoint();
  void scanArchive();
  void startCheckpoint(uint32_t nowMs);
  void startArchive(uint32_t nowMs);
  void handleResponse(uint32_t nowMs);
  void schedule(uint32_t nowMs);
  bool queueRead(uint16_t address, uint8_t length);
  bool queueWrite(uint16_t address, const uint8_t* data, uint8_t length);
};

extern EnergyCalendarStore energyCalendarStore;
