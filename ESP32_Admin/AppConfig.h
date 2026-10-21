#pragma once
#include <Arduino.h>

// Shared configuration model for the ESP32 admin firmware.
static constexpr uint8_t MAX_SAVED_NETWORKS = 8;
static constexpr char CONFIG_PATH[] = "/solarpv.cfg";

struct SavedNetwork {
  String ssid;
  String password;
};

struct AppConfig {
  String ssid;
  String password;
  String title;
  bool showPower;
  bool showVoltage;
  bool showCurrent;
  bool showBattery;
  uint8_t fontScale;
  int powerX, powerY, powerW, powerH;
  int voltageX, voltageY, voltageW, voltageH;
  int currentX, currentY, currentW, currentH;
  int batteryX, batteryY, batteryW, batteryH;
  uint8_t tftPage;
};

extern AppConfig cfg;
extern SavedNetwork savedNetworks[MAX_SAVED_NETWORKS];
extern bool fsReady;

String sanitizeTitle(String s);
int clampInt(int value, int low, int high);
void setDefaults();
void loadConfig();
int findSavedNetwork(const String& ssid);
bool addOrUpdateSavedNetwork(const String& ssid, const String& password);
bool saveConfig();
