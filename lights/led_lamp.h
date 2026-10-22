#pragma once

#include <Arduino.h>

// BLE LED strip (ambient light under the bed): commands go to characteristic FFD9,
// status packets come back as notifications on FFD4.
// NimBLEDevice::init() must be called before begin().
namespace LedLamp {

struct Status {
  bool received;        // false until the lamp has sent a status packet
  bool on;
  int r, g, b;
  int white;
  int mode;
  int speed;
  unsigned long ageMs;  // time since the last status packet
};

// Prepares the BLE client. Does not connect yet and never blocks.
void begin();

// Call from loop(). Connects in the background and reconnects after a
// dropped connection. While the lamp is unreachable, the wait between
// attempts grows from BLE_RETRY_MIN_MS to BLE_RETRY_MAX_MS.
// Only blocks briefly (service discovery) right after a connection is made.
void loop();

// True when the lamp is connected and ready for commands.
bool isConnected();

// Try to connect right away and restart the backoff. Never blocks.
// Commands call this themselves when the lamp is not connected.
void connectNow();

// If not connected: tries right away and waits until the lamp is ready,
// at most LED_LAMP_CONNECT_TIMEOUT_MS + 2 s. Blocks everything meanwhile.
bool ensureConnected();

bool setColor(uint8_t r, uint8_t g, uint8_t b);
bool setWhite(uint8_t white);  // just all rgb values the same
bool powerOn();
bool powerOff();

// The answer arrives asynchronously; read it with lastStatus().
bool requestStatus();

// Asks for the status and waits for the answer, at most
// LED_LAMP_STATUS_TIMEOUT_MS. Blocks everything meanwhile. Returns false if
// the lamp is not connected or didn't answer in time.
bool fetchStatus();

// Latest status received from the lamp.
Status lastStatus();

}  // namespace LedLamp
