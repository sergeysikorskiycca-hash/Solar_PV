/*
  Asynchronous 24C32 access requests. The ESP8266 remains the only I2C master.
*/
#include "EnergyEepromTransport.h"
#include <string.h>

EnergyEepromTransport energyEepromTransport;

bool EnergyEepromTransport::queueRead(uint16_t address, uint8_t length) {
  if ((state_ != IDLE && state_ != COMPLETE && state_ != FAILED) ||
      address >= 4096 || length == 0 || length > 8 ||
      (uint32_t)address + length > 4096UL) return false;
  commandType_ = 4;
  address_ = address;
  length_ = length;
  memset(data_, 0, sizeof(data_));
  resultStatus_ = 0xFF;
  state_ = QUEUED;
  return true;
}

bool EnergyEepromTransport::queueWrite(uint16_t address, const uint8_t* data, uint8_t length) {
  if (!data || (state_ != IDLE && state_ != COMPLETE && state_ != FAILED) ||
      address >= 4096 || length == 0 || length > 8 ||
      (uint32_t)address + length > 4096UL ||
      ((address & 0x1F) + length) > 32) return false;
  commandType_ = 5;
  address_ = address;
  length_ = length;
  memcpy(data_, data, length);
  if (length < sizeof(data_)) memset(data_ + length, 0, sizeof(data_) - length);
  resultStatus_ = 0xFF;
  state_ = QUEUED;
  return true;
}

bool EnergyEepromTransport::takeCommand(uint8_t& type, uint8_t& sequence,
                                        uint16_t& address, uint8_t& length,
                                        uint8_t data[8]) {
  if (state_ != QUEUED) return false;
  ++sequence_;
  type = commandType_;
  sequence = sequence_;
  address = address_;
  length = length_;
  memcpy(data, data_, sizeof(data_));
  state_ = WAITING;
  return true;
}

void EnergyEepromTransport::acceptResponse(const uint8_t* frame, uint8_t length) {
  if (!frame || length != 16 || frame[0] != 0xA8 || frame[1] != 3) return;
  uint8_t crc = 0;
  for (uint8_t i = 0; i < 15; ++i) {
    crc ^= frame[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  if (crc != frame[15] || state_ != WAITING || frame[2] != sequence_ ||
      (uint16_t)(frame[3] | ((uint16_t)frame[4] << 8)) != address_ ||
      frame[5] != length_) return;
  resultStatus_ = frame[6];
  memcpy(data_, frame + 7, sizeof(data_));
  state_ = resultStatus_ == 0 ? COMPLETE : FAILED;
}

EnergyEepromTransport::State EnergyEepromTransport::state() const { return state_; }
uint8_t EnergyEepromTransport::resultStatus() const { return resultStatus_; }
uint8_t EnergyEepromTransport::resultLength() const { return length_; }
const uint8_t* EnergyEepromTransport::resultData() const { return data_; }

bool EnergyEepromTransport::consume() {
  if (state_ != COMPLETE && state_ != FAILED) return false;
  state_ = IDLE;
  return true;
}
