/*
  Asynchronous command transport for the 24C32 EEPROM behind the ESP8266 gateway.
  Transfers are limited to eight bytes so they fit the existing I2C response frame.
*/
#pragma once
#include <Arduino.h>

class EnergyEepromTransport {
public:
  enum State : uint8_t { IDLE, QUEUED, WAITING, COMPLETE, FAILED };

  bool queueRead(uint16_t address, uint8_t length);
  bool queueWrite(uint16_t address, const uint8_t* data, uint8_t length);
  bool takeCommand(uint8_t& type, uint8_t& sequence, uint16_t& address,
                   uint8_t& length, uint8_t data[8]);
  void acceptResponse(const uint8_t* frame, uint8_t length);
  State state() const;
  uint8_t resultStatus() const;
  uint8_t resultLength() const;
  const uint8_t* resultData() const;
  bool consume();

private:
  State state_ = IDLE;
  uint8_t commandType_ = 0;
  uint8_t sequence_ = 0;
  uint16_t address_ = 0;
  uint8_t length_ = 0;
  uint8_t data_[8] = {};
  uint8_t resultStatus_ = 0xFF;
};

extern EnergyEepromTransport energyEepromTransport;
