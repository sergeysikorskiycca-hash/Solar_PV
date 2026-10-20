#include "ConfigStore.h"
#include <LittleFS.h>

AppConfig cfg;
SavedNetwork savedNetworks[MAX_SAVED_NETWORKS];
bool fsReady = false;

String sanitizeTitle(String s) {
  s.trim();
  String out;
  out.reserve(54);
  // Preserve complete UTF-8 sequences for Russian and Latin titles.
  for (size_t i = 0; i < s.length() && out.length() < 54; i++) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x80) {
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' ||
          c == '.' || c == ':' || c == '/') out += (char)c;
      continue;
    }
    uint8_t count = (c & 0xE0) == 0xC0 ? 2 :
                    ((c & 0xF0) == 0xE0 ? 3 :
                    ((c & 0xF8) == 0xF0 ? 4 : 0));
    if (count == 0 || i + count > s.length() || out.length() + count > 54) continue;
    bool valid = true;
    for (uint8_t j = 1; j < count; j++) {
      if (((uint8_t)s[i + j] & 0xC0) != 0x80) valid = false;
    }
    if (valid) {
      out += s.substring(i, i + count);
      i += count - 1;
    }
  }
  if (out.length() == 0) out = "SOLAR PV";
  return out;
}

int clampInt(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

void setDefaults() {
  cfg.ssid = "";
  cfg.password = "";
  cfg.title = "SOLAR PV";
  cfg.showPower = true;
  cfg.showVoltage = true;
  cfg.showCurrent = true;
  cfg.showBattery = true;
  cfg.fontScale = 1;
  cfg.powerX = 8; cfg.powerY = 55;
  cfg.voltageX = 8; cfg.voltageY = 125;
  cfg.currentX = 125; cfg.currentY = 125;
  cfg.batteryX = 8; cfg.batteryY = 205;
  cfg.tftPage = 1;
}

void loadConfig() {
  setDefaults();
  if (!fsReady) return;
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return;
  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; i++) { savedNetworks[i].ssid = ""; savedNetworks[i].password = ""; }
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq < 1) continue;
    String key = line.substring(0, eq);
    String value = line.substring(eq + 1);
    if (key == "ssid") cfg.ssid = value;
    else if (key == "password") cfg.password = value;
    else if (key == "title") cfg.title = sanitizeTitle(value);
    else if (key == "showPower") cfg.showPower = value == "1";
    else if (key == "showVoltage") cfg.showVoltage = value == "1";
    else if (key == "showCurrent") cfg.showCurrent = value == "1";
    else if (key == "showBattery") cfg.showBattery = value == "1";
    else if (key == "fontScale") cfg.fontScale = value == "2" ? 2 : 1;
    else if (key == "powerX") cfg.powerX = clampInt(value.toInt(), 0, 16);
    else if (key == "powerY") cfg.powerY = clampInt(value.toInt(), 40, 230);
    else if (key == "voltageX") cfg.voltageX = clampInt(value.toInt(), 0, 128);
    else if (key == "voltageY") cfg.voltageY = clampInt(value.toInt(), 40, 240);
    else if (key == "currentX") cfg.currentX = clampInt(value.toInt(), 0, 128);
    else if (key == "currentY") cfg.currentY = clampInt(value.toInt(), 40, 240);
    else if (key == "batteryX") cfg.batteryX = clampInt(value.toInt(), 0, 16);
    else if (key == "batteryY") cfg.batteryY = clampInt(value.toInt(), 40, 240);
    else if (key == "tftPage") cfg.tftPage = value == "2" ? 2 : 1;
    else if (key.startsWith("wifi") && key.endsWith("_ssid")) { int i = key.substring(4, key.length() - 5).toInt(); if (i >= 0 && i < MAX_SAVED_NETWORKS) savedNetworks[i].ssid = value; }
    else if (key.startsWith("wifi") && key.endsWith("_password")) { int i = key.substring(4, key.length() - 9).toInt(); if (i >= 0 && i < MAX_SAVED_NETWORKS) savedNetworks[i].password = value; }
  }
  f.close();
  // Migrate the currently configured network from the older single-network config.
  if (cfg.ssid.length() && findSavedNetwork(cfg.ssid) < 0) addOrUpdateSavedNetwork(cfg.ssid, cfg.password);
}

int findSavedNetwork(const String& ssid) {
  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; i++) if (ssid.length() && savedNetworks[i].ssid == ssid) return i;
  return -1;
}

bool addOrUpdateSavedNetwork(const String& ssid, const String& password) {
  int index = findSavedNetwork(ssid);
  if (index < 0) for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; i++) if (!savedNetworks[i].ssid.length()) { index = i; break; }
  if (index < 0) return false;
  savedNetworks[index].ssid = ssid; savedNetworks[index].password = password;
  return true;
}

bool saveConfig() {
  if (!fsReady) return false;
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) return false;
  f.println("ssid=" + cfg.ssid);
  f.println("password=" + cfg.password);
  f.println("title=" + sanitizeTitle(cfg.title));
  f.println(String("showPower=") + (cfg.showPower ? "1" : "0"));
  f.println(String("showVoltage=") + (cfg.showVoltage ? "1" : "0"));
  f.println(String("showCurrent=") + (cfg.showCurrent ? "1" : "0"));
  f.println(String("showBattery=") + (cfg.showBattery ? "1" : "0"));
  f.println(String("fontScale=") + String(cfg.fontScale));
  f.println("powerX=" + String(cfg.powerX));
  f.println("powerY=" + String(cfg.powerY));
  f.println("voltageX=" + String(cfg.voltageX));
  f.println("voltageY=" + String(cfg.voltageY));
  f.println("currentX=" + String(cfg.currentX));
  f.println("currentY=" + String(cfg.currentY));
  f.println("batteryX=" + String(cfg.batteryX));
  f.println("batteryY=" + String(cfg.batteryY));
  f.println("tftPage=" + String(cfg.tftPage));
  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; i++) {
    f.println("wifi" + String(i) + "_ssid=" + savedNetworks[i].ssid);
    f.println("wifi" + String(i) + "_password=" + savedNetworks[i].password);
  }
  f.close();
  return true;
}
