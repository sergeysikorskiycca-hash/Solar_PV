/*
  SOLAR_STRING_1:
  INA226 -> ESP-NOW, OTA по команде от шлюза.
  Коэффициент калибровки тока хранится в EEPROM.
*/

#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <string.h>

#include "WiFiConfig.h"

extern "C" {
#include <espnow.h>
}

#define INA226_ADDR 0x44

#define SDA_PIN 14  // D5, GPIO14 — SDA
#define SCL_PIN 12  // D6, GPIO12 — SCL

// Базовое сопротивление шунта по измерению при 5 А
#define SHUNT_OHMS 0.0031734f

#define ESPNOW_CMD_OTA 0x01

// Параметры энергонезависимого хранения калибровки
//#define EEPROM_MAGIC 0x53484331UL
#define EEPROM_MAGIC 0x53484332UL


struct CalibrationRecord
{
    uint32_t magic;
    float currentCalibration;
};

const float DEFAULT_CURRENT_CALIBRATION = 1.15f;

float currentCalibration = DEFAULT_CURRENT_CALIBRATION;

uint8_t receiverMac[] = {
    0xBC, 0xFF, 0x4D, 0xF9, 0x96, 0x5D
};

struct SensorData
{
    char name[20];
    uint8_t sensorId;

    float voltage;
    float current;
    float power;
    float shuntVoltage;

    uint32_t packetNumber;
};

struct ESPNowCommand
{
    uint8_t command;
    uint8_t sensorId;
};

SensorData data;

uint32_t packetCounter = 0;

volatile bool otaRequested = false;
bool otaMode = false;


// Сохраняет коэффициент калибровки в EEPROM
bool saveCurrentCalibration()
{
    CalibrationRecord record;

    record.magic = EEPROM_MAGIC;
    record.currentCalibration = currentCalibration;

    EEPROM.put(0, record);

    if (!EEPROM.commit())
    {
        Serial.println("EEPROM SAVE ERROR");
        return false;
    }

    Serial.printf(
        "Calibration saved: %.6f\n",
        currentCalibration
    );

    return true;
}


// Загружает коэффициент при запуске датчика
void loadCurrentCalibration()
{
    EEPROM.begin(sizeof(CalibrationRecord));

    CalibrationRecord record;

    EEPROM.get(0, record);

    if (record.magic == EEPROM_MAGIC &&
        record.currentCalibration >= 0.1f &&
        record.currentCalibration <= 10.0f)
    {
        currentCalibration = record.currentCalibration;

        Serial.printf(
            "Calibration loaded: %.6f\n",
            currentCalibration
        );
    }
    else
    {
        currentCalibration = DEFAULT_CURRENT_CALIBRATION;

        Serial.println("Calibration not found");
        Serial.println("Using default: 1.000");

        saveCurrentCalibration();
    }
}


// Устанавливает новый коэффициент и сохраняет его
bool setCurrentCalibration(float newCalibration)
{
    if (!(newCalibration >= 0.1f &&
          newCalibration <= 10.0f))
    {
        Serial.println("Calibration value out of range");
        return false;
    }

    if (newCalibration == currentCalibration)
    {
        return true;
    }

    currentCalibration = newCalibration;

    return saveCurrentCalibration();
}


// Читает 16-битный регистр INA226
uint16_t ina226Read16(uint8_t reg)
{
    Wire.beginTransmission(INA226_ADDR);
    Wire.write(reg);

    if (Wire.endTransmission(false) != 0)
        return 0;

    Wire.requestFrom(
        INA226_ADDR,
        (uint8_t)2
    );

    if (Wire.available() < 2)
        return 0;

    uint16_t value = Wire.read() << 8;
    value |= Wire.read();

    return value;
}


// Инициализирует INA226
void ina226Init()
{
    uint16_t config = 0x4127;

    Wire.beginTransmission(INA226_ADDR);
    Wire.write(0x00);
    Wire.write(config >> 8);
    Wire.write(config & 0xFF);
    Wire.endTransmission();

    delay(10);
}


// Считывает измерения и корректирует ток
void readINA226()
{
    uint16_t busRaw = ina226Read16(0x02);

    int16_t shuntRaw =
        (int16_t)ina226Read16(0x01);

    float voltage =
        busRaw * 0.00125f;

    float shuntVoltage =
        shuntRaw * 0.0000025f;

    // Ток с учётом коэффициента калибровки
    float current =
        (shuntVoltage / SHUNT_OHMS)
        * currentCalibration;

    // Мощность рассчитывается по скорректированному току
    float power =
        voltage * current;

    strncpy(
        data.name,
        "SOLAR_STRING_1",
        sizeof(data.name)
    );

    data.name[sizeof(data.name) - 1] = '\0';

    data.sensorId = 1;

    data.voltage = voltage;
    data.current = current;
    data.power = power;

    data.shuntVoltage =
        shuntVoltage * 1000.0f;

    data.packetNumber =
        packetCounter++;
}


// Результат отправки ESP-NOW
void sendCallback(
    uint8_t *mac_addr,
    uint8_t sendStatus
)
{
    Serial.print("ESP-NOW: ");

    if (sendStatus == 0)
        Serial.println("OK");
    else
        Serial.println("ERROR");
}


// Принимает команду OTA от шлюза
void receiveCallback(
    uint8_t *mac,
    uint8_t *incomingData,
    uint8_t len
)
{
    if (len != sizeof(ESPNowCommand))
        return;

    ESPNowCommand command;

    memcpy(
        &command,
        incomingData,
        sizeof(command)
    );

    if (command.command != ESPNOW_CMD_OTA)
        return;

    if (command.sensorId != 1)
        return;

    Serial.println();
    Serial.println("OTA COMMAND RECEIVED");

    otaRequested = true;
}


// Переходит в режим OTA
void startOTAMode()
{
    Serial.println();
    Serial.println("STOP ESP-NOW");

    esp_now_deinit();

    delay(100);

    wifiStartOTA();

    otaMode = true;

    Serial.println("OTA READY");
    Serial.println("Connect laptop to:");
    Serial.println("SOLAR_STRING_1");
}


void setup()
{
    Serial.begin(115200);

    delay(500);

    Serial.println();
    Serial.println("SOLAR_STRING_1");
    Serial.println("INA226 -> ESP-NOW");

    // Восстанавливаем калибровку из EEPROM
    loadCurrentCalibration();

    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());

    Serial.print("WiFi channel: ");
    Serial.println(WiFi.channel());

    Wire.begin(
        SDA_PIN,
        SCL_PIN
    );

    Wire.setClock(100000);

    ina226Init();

    WiFi.mode(WIFI_STA);

    wifi_set_channel(WIFI_CHANNEL);

    if (esp_now_init() != 0)
    {
        Serial.println("ESP-NOW INIT ERROR");
        return;
    }

    esp_now_set_self_role(
        ESP_NOW_ROLE_CONTROLLER
    );

    esp_now_register_send_cb(
        sendCallback
    );

    esp_now_register_recv_cb(
        receiveCallback
    );

    uint8_t peerResult = esp_now_add_peer(
        receiverMac,
        ESP_NOW_ROLE_SLAVE,
        WIFI_CHANNEL,
        NULL,
        0
    );

    Serial.printf(
        "PEER ADD RESULT = %d\n",
        peerResult
    );

    if (peerResult != 0)
    {
        Serial.println("PEER ADD ERROR");
        return;
    }

    Serial.println("ESP-NOW READY");

    Serial.println(
        "Receiver: BC:FF:4D:F9:96:5D"
    );
}


void loop()
{
    if (otaRequested && !otaMode)
    {
        otaRequested = false;

        startOTAMode();
    }

    if (otaMode)
    {
        wifiHandle();
        return;
    }

    readINA226();

    Serial.print(data.name);
    Serial.print("  ");

    Serial.print("U=");
    Serial.print(data.voltage, 3);
    Serial.print(" V  ");

    Serial.print("I=");
    Serial.print(data.current, 3);
    Serial.print(" A  ");

    Serial.print("P=");
    Serial.print(data.power, 2);
    Serial.print(" W  ");

    Serial.print("Shunt=");
    Serial.print(data.shuntVoltage, 3);
    Serial.print(" mV  ");

    Serial.print("K=");
    Serial.print(currentCalibration, 3);
    Serial.print("  ");

    Serial.print("Packet=");
    Serial.println(data.packetNumber);

    uint8_t sendResult = esp_now_send(
        receiverMac,
        (uint8_t *)&data,
        sizeof(data)
    );

    Serial.printf(
        "ESP-NOW SEND RESULT = %d\n",
        sendResult
    );

    delay(500);
}
