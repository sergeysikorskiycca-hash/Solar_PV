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

#define INA226_FIRST_ADDR  0x44
#define INA226_SECOND_ADDR 0x45
#define SENSOR_COUNT 2
#define SENSOR_INTERVAL_MS 250UL

#define SDA_PIN 14  // D5, GPIO14 — SDA
#define SCL_PIN 12  // D6, GPIO12 — SCL

// Базовое сопротивление шунта по измерению при 5 А
#define SHUNT_OHMS 0.0031734f

#define ESPNOW_CMD_OTA 0x01
#define ESPNOW_CMD_CALIBRATION 0x02
#define EEPROM_MAGIC_V2 0x53484332UL
#define EEPROM_MAGIC 0x53484333UL

struct CalibrationRecord { uint32_t magic; float currentCalibration[SENSOR_COUNT]; float voltageCalibration[SENSOR_COUNT]; };
struct LegacyCalibrationRecord { uint32_t magic; float currentCalibration; };
float currentCalibration[SENSOR_COUNT] = { 1.00f, 1.00f };
float voltageCalibration[SENSOR_COUNT] = { 1.00f, 1.00f };
static bool eepromReady = false;

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
    uint16_t currentCalibrationX600;
    uint16_t voltageCalibrationX600;

    uint32_t packetNumber;
};

struct ESPNowCommand { uint8_t command; uint8_t sensorId; };
struct __attribute__((packed)) ESPNowCalibrationCommand { uint8_t command; uint8_t sensorId; float currentFactor; float voltageFactor; };

SensorData sensorData[SENSOR_COUNT];
uint32_t packetCounter[SENSOR_COUNT] = {0, 0};

uint32_t lastSensorSendMs = 0;
uint8_t nextSensorIndex = 0;

volatile bool otaRequested = false;
bool otaMode = false;
static volatile bool calibrationCommandPending = false;
static ESPNowCalibrationCommand pendingCalibrationCommand = {};


// Сохраняет коэффициенты обоих каналов в EEPROM.
bool saveCalibration() {
    if (!eepromReady) eepromReady = EEPROM.begin(sizeof(CalibrationRecord));
    if (!eepromReady) { Serial.println("EEPROM INIT ERROR"); return false; }
    CalibrationRecord record = {};
    record.magic = EEPROM_MAGIC;
    memcpy(record.currentCalibration, currentCalibration, sizeof(currentCalibration));
    memcpy(record.voltageCalibration, voltageCalibration, sizeof(voltageCalibration));
    EEPROM.put(0, record);
    if (!EEPROM.commit()) { Serial.println("EEPROM SAVE ERROR"); return false; }

    // Reopen the emulated EEPROM so verification reads the flash contents,
    // not only the RAM cache that was just written.
    EEPROM.end();
    eepromReady = EEPROM.begin(sizeof(CalibrationRecord));
    if (!eepromReady) { Serial.println("EEPROM REOPEN ERROR"); return false; }
    CalibrationRecord verify = {};
    EEPROM.get(0, verify);
    if (memcmp(&verify, &record, sizeof(record)) != 0) {
        Serial.println("EEPROM VERIFY ERROR");
        return false;
    }
    Serial.println("Calibration factors saved and verified in EEPROM");
    return true;
}

// Загружает новую запись или переносит прежнюю калибровку v2.
void loadCurrentCalibration() {
    eepromReady = EEPROM.begin(sizeof(CalibrationRecord));
    if (!eepromReady) { Serial.println("EEPROM INIT ERROR; calibration defaults remain active"); return; }
    uint32_t storedMagic = 0;
    EEPROM.get(0, storedMagic);
    if (storedMagic == EEPROM_MAGIC) {
        CalibrationRecord record = {};
        EEPROM.get(0, record);
        for (uint8_t i = 0; i < SENSOR_COUNT; ++i) {
            if (isfinite(record.currentCalibration[i]) && record.currentCalibration[i] >= 0.001f && record.currentCalibration[i] <= 99.999f) currentCalibration[i] = record.currentCalibration[i];
            if (isfinite(record.voltageCalibration[i]) && record.voltageCalibration[i] >= 0.001f && record.voltageCalibration[i] <= 99.999f) voltageCalibration[i] = record.voltageCalibration[i];
        }
        Serial.println("Calibration v3 loaded");
        return;
    }
    if (storedMagic == EEPROM_MAGIC_V2) {
        LegacyCalibrationRecord legacy = {};
        EEPROM.get(0, legacy);
        if (isfinite(legacy.currentCalibration) && legacy.currentCalibration >= 0.1f && legacy.currentCalibration <= 99.999f) currentCalibration[0] = legacy.currentCalibration;
        Serial.println("Legacy calibration migrated to v3");
    } else Serial.println("Calibration not found; using defaults");
    saveCalibration();
}

static bool setCalibration(uint8_t sensorId, float currentFactor, float voltageFactor) {
    if (sensorId < 1 || sensorId > SENSOR_COUNT || !isfinite(currentFactor) || !isfinite(voltageFactor) ||
        currentFactor < 0.001f || currentFactor > 99.999f || voltageFactor < 0.001f || voltageFactor > 99.999f) return false;
    uint8_t index = sensorId - 1;
    const float oldCurrentFactor = currentCalibration[index];
    const float oldVoltageFactor = voltageCalibration[index];
    currentCalibration[index] = currentFactor;
    voltageCalibration[index] = voltageFactor;
    if (!saveCalibration()) {
        currentCalibration[index] = oldCurrentFactor;
        voltageCalibration[index] = oldVoltageFactor;
        Serial.println("Calibration not applied: EEPROM commit failed; previous factors restored");
        return false;
    }
    Serial.printf("Calibration sensor %u: I=%.3f U=%.3f\n", (unsigned)sensorId, currentFactor, voltageFactor);
    return true;
}

// Читает 16-битный регистр выбранного INA226
uint16_t ina226Read16(uint8_t address, uint8_t reg)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom(address, (uint8_t)2) != 2) return 0;
    uint16_t value = (uint16_t)Wire.read() << 8;
    value |= Wire.read();
    return value;
}

// Инициализирует INA226 и подробно проверяет запись конфигурации
bool ina226Init(uint8_t address)
{
    const uint16_t config = 0x4127;

    for (uint8_t attempt = 0; attempt < 3; attempt++)
    {
        Wire.beginTransmission(address);
        Wire.write(0x00);
        Wire.write(config >> 8);
        Wire.write(config & 0xFF);
        uint8_t writeResult = Wire.endTransmission();

        if (writeResult != 0)
        {
            Serial.printf(
                "INA226 0x%02X write error=%u attempt=%u\n",
                address, writeResult, attempt + 1
            );
            continue;
        }

        Wire.beginTransmission(address);
        Wire.write(0x00);
        uint8_t selectResult = Wire.endTransmission(false);

        if (selectResult != 0)
        {
            Serial.printf(
                "INA226 0x%02X register select error=%u attempt=%u\n",
                address, selectResult, attempt + 1
            );
            continue;
        }

        uint8_t received = Wire.requestFrom(address, (uint8_t)2);

        if (received != 2)
        {
            while (Wire.available())
                Wire.read();

            Serial.printf(
                "INA226 0x%02X read bytes=%u attempt=%u\n",
                address, received, attempt + 1
            );
            continue;
        }

        uint16_t readBack = (uint16_t)Wire.read() << 8;
        readBack |= Wire.read();

        Serial.printf(
            "INA226 0x%02X config readback=0x%04X attempt=%u\n",
            address, readBack, attempt + 1
        );

        if (readBack == config)
            return true;
    }

    return false;
}

// Считывает измерения выбранного датчика и формирует его пакет
void readINA226(uint8_t index)
{
    const uint8_t address = (index == 0) ? INA226_FIRST_ADDR : INA226_SECOND_ADDR;
    const uint8_t sensorId = index + 1;
    SensorData &data = sensorData[index];

    uint16_t busRaw = ina226Read16(address, 0x02);
    int16_t shuntRaw = (int16_t)ina226Read16(address, 0x01);
    float voltage = busRaw * 0.00125f;
    float shuntVoltage = shuntRaw * 0.0000025f;
    voltage *= voltageCalibration[index];
    float current = (shuntVoltage / SHUNT_OHMS) * currentCalibration[index];
    float power = voltage * current;

    snprintf(data.name, sizeof(data.name), "SOLAR_STRING_%u", (unsigned)sensorId);
    data.sensorId = sensorId;
    data.voltage = voltage;
    data.current = current;
    data.power = power;
    data.shuntVoltage = shuntVoltage * 1000.0f;
    data.currentCalibrationPermille = (uint16_t)lroundf(currentCalibration[index] * 600.0f);
    data.voltageCalibrationPermille = (uint16_t)lroundf(voltageCalibration[index] * 600.0f);
    data.packetNumber = packetCounter[index]++;
}

// Печатает и отправляет пакет выбранного датчика
void sendSensorData(uint8_t index)
{
    SensorData &data = sensorData[index];
    Serial.print(data.name);
    Serial.print("  U="); Serial.print(data.voltage, 3); Serial.print(" V  ");
    Serial.print("I="); Serial.print(data.current, 3); Serial.print(" A  ");
    Serial.print("P="); Serial.print(data.power, 2); Serial.print(" W  ");
    Serial.print("Shunt="); Serial.print(data.shuntVoltage, 3); Serial.print(" mV  ");
    Serial.print("K="); Serial.print(currentCalibration[index], 3); Serial.print(" KU="); Serial.print(voltageCalibration[index], 3); Serial.print("  Packet=");
    Serial.println(data.packetNumber);

    uint8_t sendResult = esp_now_send(receiverMac, (uint8_t *)&data, sizeof(data));
    Serial.printf("ESP-NOW SEND RESULT = %d\n", sendResult);
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
    if (len == sizeof(ESPNowCalibrationCommand)) {
        ESPNowCalibrationCommand calibration = {};
        memcpy(&calibration, incomingData, sizeof(calibration));
        if (calibration.command == ESPNOW_CMD_CALIBRATION) {
            pendingCalibrationCommand = calibration;
            calibrationCommandPending = true;
            return;
        }
    }
    if (len != sizeof(ESPNowCommand)) return;
    ESPNowCommand command;
    memcpy(&command, incomingData, sizeof(command));
    if (command.command != ESPNOW_CMD_OTA || command.sensorId != 1) return;
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

    wifiStartOTA();

    otaMode = true;

    Serial.println("OTA READY");
    Serial.println("Connect laptop to:");
    Serial.println("SOLAR_STRING_1");
}


void setup()
{
    Serial.begin(115200);

    Serial.println();
    Serial.println("SOLAR_STRING_1");
    Serial.println("INA226 -> ESP-NOW");

    // Восстанавливаем калибровку из EEPROM
    loadCurrentCalibration();

    // Сначала настраиваем Wi-Fi и фиксируем канал ESP-NOW
    WiFi.mode(WIFI_STA);
    wifi_set_channel(WIFI_CHANNEL);

    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());

    Serial.print("WiFi channel: ");
    Serial.println(WiFi.channel());

    // После настройки Wi-Fi запускаем I2C
    Wire.begin(
        SDA_PIN,
        SCL_PIN
    );

    Wire.setClock(100000);

    Serial.printf("INA226 0x%02X: %s\n", INA226_FIRST_ADDR, ina226Init(INA226_FIRST_ADDR) ? "READY" : "NOT FOUND");
    Serial.printf("INA226 0x%02X: %s\n", INA226_SECOND_ADDR, ina226Init(INA226_SECOND_ADDR) ? "READY" : "NOT FOUND");

    if (esp_now_init() != 0)
    {
        Serial.println("ESP-NOW INIT ERROR");
        return;
    }

    esp_now_set_self_role(
        ESP_NOW_ROLE_COMBO
    );

    esp_now_register_send_cb(
        sendCallback
    );

    esp_now_register_recv_cb(
        receiveCallback
    );

    uint8_t peerResult = esp_now_add_peer(
        receiverMac,
        ESP_NOW_ROLE_COMBO,
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
    if (calibrationCommandPending) {
        ESPNowCalibrationCommand command;
        noInterrupts();
        command = pendingCalibrationCommand;
        calibrationCommandPending = false;
        interrupts();
        setCalibration(command.sensorId, command.currentFactor, command.voltageFactor);
    }

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

    uint32_t now = millis();

    // Датчики отправляются по очереди; каждый передаёт данные раз в 500 мс.
    if (now - lastSensorSendMs >= SENSOR_INTERVAL_MS)
    {
        lastSensorSendMs = now;
        readINA226(nextSensorIndex);
        sendSensorData(nextSensorIndex);
        nextSensorIndex++;
        if (nextSensorIndex >= SENSOR_COUNT) nextSensorIndex = 0;
    }
}
