#pragma once
#include <Arduino.h>

// Shared configuration model for the ESP32 admin firmware.
static constexpr uint8_t MAX_SAVED_NETWORKS = 8;
static constexpr uint8_t MAX_PAGES = 4;
static constexpr uint8_t MAX_WIDGETS_PER_PAGE = 12;
static constexpr char CONFIG_PATH[] = "/solarpv.cfg";

struct SavedNetwork {
  String ssid;
  String password;
};

// Widget types are data, not page-specific drawing code.
struct WidgetConfig {
  String type;       // power, voltage, current, battery, wifiStatus
  String label;
  String text;      // Free text for text widgets; supports UTF-8 and line breaks.
  uint8_t headingSize, contentSize;
  uint8_t stringCount; // Number of PV strings shown by a strings widget.
  bool locked; // Prevents accidental position and size changes in the web editor.
  uint8_t headingAlign, contentAlign;
  uint8_t headingColor, contentColor;
  uint8_t backgroundColor; // 0 = near-black; 1..5 = dark background presets
  int x, y, w, h;
  bool enabled;
};

struct PageConfig {
  String title;
  uint8_t widgetCount;
  WidgetConfig widgets[MAX_WIDGETS_PER_PAGE];
};

struct AppConfig {
  String ssid;
  String password;
  String title;
  uint8_t pageCount;
  uint8_t tftPage;   // 1-based physical display page
  PageConfig pages[MAX_PAGES];

  // Legacy fields are retained only to migrate existing /solarpv.cfg files.
  String powerLabel, voltageLabel, currentLabel, batteryLabel;
  bool showPower, showVoltage, showCurrent, showBattery;
  uint8_t fontScale;
  int powerX, powerY, powerW, powerH;
  int voltageX, voltageY, voltageW, voltageH;
  int currentX, currentY, currentW, currentH;
  int batteryX, batteryY, batteryW, batteryH;
};

extern AppConfig cfg;
extern SavedNetwork savedNetworks[MAX_SAVED_NETWORKS];
extern bool fsReady;

String sanitizeTitle(String s);
int clampInt(int value, int low, int high);
void setDefaults();
void initializeDefaultPages();
void loadConfig();
int findSavedNetwork(const String& ssid);
bool addOrUpdateSavedNetwork(const String& ssid, const String& password);
bool saveConfig();
bool isKnownWidgetType(const String& type);
