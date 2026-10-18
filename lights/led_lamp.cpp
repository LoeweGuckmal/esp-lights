#include "led_lamp.h"

#include <NimBLEDevice.h>

#include "config.h"

namespace LedLamp {
namespace {

// Characteristics used by the lamp:
//   write:  0000ffd9-0000-1000-8000-00805f9b34fb
//   notify: 0000ffd4-0000-1000-8000-00805f9b34fb
NimBLEAddress lampAddress(std::string(LED_LAMP_ADDRESS), LED_LAMP_ADDRESS_TYPE);

// Created once in begin() and reused for every connection attempt.
NimBLEClient* client = nullptr;
NimBLERemoteCharacteristic* writeChar = nullptr;
NimBLERemoteCharacteristic* notifyChar = nullptr;

// Connection state. The volatile flags are set from the BLE task by
// LampCallbacks and handled in loop(), because service discovery must not
// run inside a BLE callback.
volatile bool attemptPending = false;  // a connection attempt is running
volatile bool justConnected = false;   // connected, discovery still to do
volatile bool ready = false;           // discovery done, commands can be sent
unsigned long lastAttemptMs = 0;
// When to try next. While the lamp is unreachable, the wait doubles
// after every attempt (backoff), see BLE_RETRY_MIN_MS in config.h.
unsigned long nextAttemptMs = 0;
uint32_t retryDelayMs = BLE_RETRY_MIN_MS;

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

// Runs in the BLE task: only set flags here.
class LampCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pClient) override {
    attemptPending = false;
    justConnected = true;
  }

  void onConnectFail(NimBLEClient* pClient, int reason) override {
    attemptPending = false;
    Serial.print("LED strip connection failed, reason ");
    Serial.println(reason);
  }

  void onDisconnect(NimBLEClient* pClient, int reason) override {
    ready = false;
    justConnected = false;
    attemptPending = false;
    Serial.print("LED strip disconnected, reason ");
    Serial.println(reason);
  }
};

LampCallbacks callbacks;

bool sendCommand(const uint8_t* data, size_t length) {
  if (!isConnected()) {
    Serial.println("LED strip not connected!");
    connectNow();
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

// Starts a connection attempt and returns immediately.
// The result arrives in LampCallbacks.
void startConnecting() {
  Serial.println("Connecting to LED strip...");

  lastAttemptMs = millis();

  // connect() deletes the old services, so drop our pointers into them first.
  writeChar = nullptr;
  notifyChar = nullptr;

  attemptPending = true;
  if (!client->connect(lampAddress, true, true)) {
    attemptPending = false;
    // Usually the room light is connecting right now (only one attempt can
    // run at a time), so try again in a second.
    nextAttemptMs = millis() + 1000;
    Serial.println("Could not start connecting to LED strip.");
    return;
  }

  // If this attempt fails, wait retryDelayMs, and twice as long after the next one.
  nextAttemptMs = millis() + retryDelayMs;
  retryDelayMs = min(retryDelayMs * 2, BLE_RETRY_MAX_MS);
}

// Called once after each new connection: finds the characteristics
// and subscribes to status notifications. Blocks for a moment.
bool setupConnection() {
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

  if (!client->isConnected()) {
    Serial.println("LED strip disconnected during setup.");
    return false;
  }

  ready = true;

  // Reachable again: after the next drop, retry right away and start
  // the backoff from the beginning.
  retryDelayMs = BLE_RETRY_MIN_MS;
  nextAttemptMs = millis();

  delay(300);
  requestStatus();

  return true;
}

}  // namespace

void begin() {
  client = NimBLEDevice::createClient(lampAddress);
  client->setClientCallbacks(&callbacks, false);
  client->setConnectTimeout(LED_LAMP_CONNECT_TIMEOUT_MS);
}

void loop() {
  if (!client) return;

  if (justConnected) {
    justConnected = false;
    if (!setupConnection()) {
      client->disconnect();
    }
    return;
  }

  // Safety net in case the stack never reports the end of an attempt.
  if (attemptPending &&
      millis() - lastAttemptMs > LED_LAMP_CONNECT_TIMEOUT_MS + 5000) {
    attemptPending = false;
  }

  if (ready || attemptPending || client->isConnected()) return;

  // Signed difference keeps working when millis() wraps around.
  if ((long)(millis() - nextAttemptMs) < 0) return;

  startConnecting();
}

bool isConnected() {
  return ready && client && client->isConnected() && writeChar;
}

void connectNow() {
  retryDelayMs = BLE_RETRY_MIN_MS;
  nextAttemptMs = millis();
}

bool ensureConnected() {
  if (isConnected()) return true;

  connectNow();

  unsigned long start = millis();
  while (!isConnected() &&
         millis() - start < LED_LAMP_CONNECT_TIMEOUT_MS + 2000) {
    loop();
    delay(10);
  }
  return isConnected();
}

bool setColor(uint8_t r, uint8_t g, uint8_t b) {
  const uint8_t command[] = {
    0x56, r, g, b, 0x00, 0xF0, 0xAA
  };

  return sendCommand(command, sizeof(command));
}

bool setWhite(uint8_t white) {
  const uint8_t command[] = {
    0x56, white, white, white, 0x00, 0xF0, 0xAA
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
