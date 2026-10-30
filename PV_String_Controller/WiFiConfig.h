#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>

#define WIFI_AP_NAME "SOLAR_STRING_1"
#define WIFI_CHANNEL 1

void wifiStartOTA()
{
    WiFi.mode(WIFI_AP);

    WiFi.softAP(
        WIFI_AP_NAME,
        nullptr,
        WIFI_CHANNEL,
        false,
        1
    );

    ArduinoOTA.setHostname("SOLAR_STRING_1");

    ArduinoOTA.onStart([]()
    {
        Serial.println();
        Serial.println("OTA START");
    });

    ArduinoOTA.onEnd([]()
    {
        Serial.println();
        Serial.println("OTA END");
    });

    ArduinoOTA.onProgress([](
        unsigned int progress,
        unsigned int total
    )
    {
        static unsigned int lastPercent = 0;

        unsigned int percent =
            (progress * 100) / total;

        if (percent >= lastPercent + 10)
        {
            lastPercent = percent;

            Serial.printf(
                "OTA: %u%%\n",
                percent
            );
        }
    });

    ArduinoOTA.onError([](ota_error_t error)
    {
        Serial.printf(
            "OTA ERROR: %u\n",
            error
        );
    });

    ArduinoOTA.begin();

    Serial.println();
    Serial.println("OTA MODE");

    Serial.print("AP: ");
    Serial.println(WIFI_AP_NAME);

    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());

    Serial.print("Channel: ");
    Serial.println(WiFi.channel());
}

void wifiHandle()
{
    ArduinoOTA.handle();
}

#endif
