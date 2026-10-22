/*
  IR_Remote.h
  Приём ИК-команд NEC для SolarPV на ESP32.
  Приёмник DATA подключается к GPIO27.
*/
#pragma once
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include "IR_Remote_Codes.h"

static constexpr uint8_t IR_REMOTE_RECEIVE_PIN = 27;
static IRrecv irRemoteReceiver(IR_REMOTE_RECEIVE_PIN, 1024, 50, true);
static decode_results irRemoteResults;

static void IR_RemoteBegin() {
  irRemoteReceiver.enableIRIn();
}

static uint32_t IR_RemoteUpdate() {
  if (!irRemoteReceiver.decode(&irRemoteResults)) return 0;
  const uint64_t rawCode = irRemoteResults.value;
  irRemoteReceiver.resume();

  // Repeat NEC frames must not trigger repeated page changes.
  if (rawCode == 0xFFFFFFFFULL || rawCode == 0xFFFFFFFFFFFFFFFFULL) return 0;
  return static_cast<uint32_t>(rawCode);
}
