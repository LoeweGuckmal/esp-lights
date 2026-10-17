#pragma once

#include <Arduino.h>

// BLE LED lamp: commands go to characteristic FFD9,
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
// dropped connection, retrying every LED_LAMP_RETRY_INTERVAL_MS.
// Only blocks briefly (service discovery) right after a connection is made.
void loop();

// True when the lamp is connected and ready for commands.
bool isConnected();

bool setColor(uint8_t r, uint8_t g, uint8_t b);
bool setWhite(uint8_t white);  // dedicated white LEDs, RGB off
bool powerOn();
bool powerOff();

// The answer arrives asynchronously; read it with lastStatus().
bool requestStatus();

// Latest status received from the lamp.
Status lastStatus();

}  // namespace LedLamp
