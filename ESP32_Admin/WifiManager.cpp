#include "WifiManager.h"
#include <WiFi.h>
#include "AppConfig.h"
#include "Display.h"

extern bool apActive;
extern unsigned long disconnectedSince;

void startAccessPoint() {
  if (apActive) return;
  if (cfg.ssid.length() > 0) WiFi.mode(WIFI_AP_STA);
  else WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  apActive = WiFi.softAP("SolarPV-Setup", "SolarPV8266");
  Serial.println();
  Serial.println(F("Setup access point requested."));
  Serial.println(F("SSID: SolarPV-Setup"));
  Serial.println(F("Open http://192.168.4.1"));
  renderTFT();
}

void connectWiFiAtBoot() {
  if (cfg.ssid.length() == 0) {
    startAccessPoint();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
  Serial.print(F("Connecting to configured Wi-Fi"));
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000UL) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    disconnectedSince = 0;
    Serial.print(F("Connected. IP: "));
    Serial.println(WiFi.localIP());
    startAccessPoint();
  } else {
    Serial.println(F("Wi-Fi connection failed; enabling setup AP."));
    startAccessPoint();
    disconnectedSince = millis();
  }
}
