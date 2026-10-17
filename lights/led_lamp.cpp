#include "led_lamp.h"

#include <NimBLEDevice.h>

#include "config.h"

namespace LedLamp {
namespace {

// Characteristics used by the lamp:
//   write:  0000ffd9-0000-1000-8000-00805f9b34fb
//   notify: 0000ffd4-0000-1000-8000-00805f9b34fb
NimBLEAddress lampAddress(std::string(LED_LAMP_ADDRESS), LED_LAMP_ADDRESS_TYPE);

NimBLEClient* client = nullptr;
NimBLERemoteCharacteristic* writeChar = nullptr;
NimBLERemoteCharacteristic* notifyChar = nullptr;

// Latest status received from the lamp (written from the BLE task)
volatile bool lampOn = false;
volatile int lampR = 0;
volatile int lampG = 0;
volatile int lampB = 0;
volatile int lampWhite = 0;
volatile int lampMode = 0;
volatile int lampSpeed = 0;
volatile unsigned long lastStatusMs = 0;
volatile bool statusReceived = false;

void printHex(const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
}

void notificationCallback(
    NimBLERemoteCharacteristic* characteristic,
    uint8_t* data,
    size_t length,
    bool isNotify
) {
  Serial.print("BLE notification: ");

  // Parse the lamp's status packet.
  // Expected example:
  // 66 E3 23 41 20 10 FF 00 00 00 03 99
  if (length >= 10) {
    lampOn = (data[2] == 0x23);
    lampMode = data[3];
    lampSpeed = data[5];

    lampR = data[6];
    lampG = data[7];
    lampB = data[8];
    lampWhite = data[9];

    lastStatusMs = millis();
    statusReceived = true;
  }

  printHex(data, length);
  Serial.println();
}

bool sendCommand(const uint8_t* data, size_t length) {
  if (!client || !client->isConnected() || !writeChar) {
    Serial.println("Not connected to lamp!");
    return false;
  }

  bool success = writeChar->writeValue(
      data, length, false
  );

  Serial.print("Sent: ");
  printHex(data, length);
  Serial.println(success ? " OK" : " FAILED");
  return success;
}

}  // namespace

bool connect() {
  Serial.println("Connecting to lamp...");

  client = NimBLEDevice::createClient();

  if (!client->connect(lampAddress)) {
    Serial.println("Connection failed.");
    return false;
  }

  Serial.println("Connected! Discovering characteristics...");

  // Discover all services and locate the characteristic
  auto services = client->getServices(true);

  for (auto* service : services) {
    auto characteristics = service->getCharacteristics(true);

    for (auto* characteristic : characteristics) {
      std::string uuid = characteristic->getUUID().toString();

      if (uuid.find("ffd9") != std::string::npos ||
          uuid.find("FFD9") != std::string::npos) {
        writeChar = characteristic;
      }

      if (uuid.find("ffd4") != std::string::npos ||
          uuid.find("FFD4") != std::string::npos) {
        notifyChar = characteristic;
      }
    }
  }

  if (!writeChar) {
    Serial.println("Write characteristic FFD9 not found.");
    return false;
  }

  Serial.println("Write characteristic found.");

  if (notifyChar && notifyChar->canNotify()) {
    if (notifyChar->subscribe(true, notificationCallback)) {
      Serial.println("Status notifications enabled.");
    } else {
      Serial.println("Could not enable notifications.");
    }
  }

  delay(300);
  requestStatus();

  return true;
}

bool setColor(uint8_t r, uint8_t g, uint8_t b) {
  const uint8_t command[] = {
    0x56, r, g, b, 0x00, 0xF0, 0xAA
  };

  return sendCommand(command, sizeof(command));
}

bool powerOn() {
  const uint8_t command[] = {0xCC, 0x23, 0x33};
  return sendCommand(command, sizeof(command));
}

bool powerOff() {
  const uint8_t command[] = {0xCC, 0x24, 0x33};
  return sendCommand(command, sizeof(command));
}

bool requestStatus() {
  const uint8_t command[] = {0xEF, 0x01, 0x77};
  return sendCommand(command, sizeof(command));
}

Status lastStatus() {
  Status status;
  status.received = statusReceived;
  status.on = lampOn;
  status.r = lampR;
  status.g = lampG;
  status.b = lampB;
  status.white = lampWhite;
  status.mode = lampMode;
  status.speed = lampSpeed;
  status.ageMs = status.received ? millis() - lastStatusMs : 0;
  return status;
}

}  // namespace LedLamp
