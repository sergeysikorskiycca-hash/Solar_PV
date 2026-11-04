#pragma once
#include <Arduino.h>
#include "UiTexts.h"

// Shared configuration model for the ESP32 admin firmware.
static constexpr uint8_t MAX_SAVED_NETWORKS = 8;
static constexpr uint8_t MAX_PAGES = 8;
static constexpr uint8_t MAX_WIDGETS_PER_PAGE = 12;
static constexpr uint8_t MAX_WIDGET_ELEMENTS = 12;
static constexpr char CONFIG_PATH[] = "/solarpv.cfg";

struct SavedNetwork {
  String ssid;
  String password;
};

// One independently configured data row inside a universal widget.
enum WidgetElementKind : uint8_t {
  ELEMENT_SENSOR = 0, ELEMENT_CLOCK = 1, ELEMENT_WIFI_STATUS = 2,
  ELEMENT_WIFI_CONTROL = 3, ELEMENT_CALIBRATION = 4,
  ELEMENT_WIFI_NETWORK = 5, ELEMENT_TEXT = 6
};

// One configurable item inside the universal widget.
struct WidgetElementConfig {
  String label;
  uint8_t kind;     // WidgetElementKind.
  uint8_t sourceId; // 0=gateway, 1..6=string sensor, 7=total PV power.
  uint8_t metric;   // Kind-specific option.
  uint8_t fontSize; // 0..5.
  uint8_t color;    // 0..5.
};

// Every widget is universal; legacy fields exist only during LittleFS migration.
struct WidgetConfig {
  String label;
  uint8_t headingSize, contentSize;
  uint8_t elementCount; // Number of configured rows in a universal widget.
  WidgetElementConfig elements[MAX_WIDGET_ELEMENTS];
  bool locked; // Prevents accidental position and size changes in the web editor
  bool showHeading; // Hide redundant widget heading when false.
  bool showBorder; // Draw widget outline when true.
  uint8_t headingAlign, contentAlign;
  uint8_t headingColor, contentColor;
  uint8_t backgroundColor; // 0..5 = dark presets; 6 = black
  int x, y, w, h;
  bool enabled;
};

struct PageConfig {
  String title;
  bool showTitle; // Whether to draw this page title on the TFT
  uint8_t widgetCount;
  WidgetConfig widgets[MAX_WIDGETS_PER_PAGE];
};

struct AppConfig {
  bool defaultRouterMode;
  bool defaultSetupAp;
  String ssid;
  String password;
  String title;
  uint8_t pageCount;
  uint8_t tftPage;   // 1-based physical display page
  float currentCalibration[6];
  float voltageCalibration[6];
  PageConfig pages[MAX_PAGES];
  UiTexts uiTexts;

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
