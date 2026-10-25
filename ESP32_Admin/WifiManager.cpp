#include "WifiManager.h"
#include <WiFi.h>
#include "AppConfig.h"
#include "Display.h"
extern bool apActive;
extern const char* AP_SSID;
extern const char* AP_PASS;
bool routerModeEnabled = false;
bool setupApEnabled = true;
void applyWiFiMode() {
  const bool wasAp = apActive;
  if (setupApEnabled && routerModeEnabled) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    if (!apActive) apActive = WiFi.softAP(AP_SSID, AP_PASS);
    if (cfg.ssid.length() && WiFi.status() != WL_CONNECTED) { WiFi.setAutoReconnect(true); WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str()); }
  } else if (setupApEnabled) {
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(false, false);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    if (!apActive) apActive = WiFi.softAP(AP_SSID, AP_PASS);
  } else if (routerModeEnabled) {
    if (apActive) WiFi.softAPdisconnect(true);
    apActive = false; WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
    if (cfg.ssid.length() && WiFi.status() != WL_CONNECTED) WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
  } else {
    if (apActive) WiFi.softAPdisconnect(true);
    apActive = false; WiFi.disconnect(true, false); WiFi.mode(WIFI_OFF);
  }
  Serial.printf("Wi-Fi mode: router=%s, setupAP=%s\n", routerModeEnabled?"ON":"OFF", setupApEnabled?"ON":"OFF");
  if (wasAp != apActive) renderTFT();
}
void connectWiFiAtBoot() {
  routerModeEnabled = cfg.defaultRouterMode; setupApEnabled = cfg.defaultSetupAp;
  WiFi.persistent(false); WiFi.setAutoReconnect(false); applyWiFiMode();
}
