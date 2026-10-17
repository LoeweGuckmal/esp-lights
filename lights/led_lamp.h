#pragma once

#include <Arduino.h>

// BLE LED lamp: commands go to characteristic FFD9,
// status packets come back as notifications on FFD4.
// NimBLEDevice::init() must be called before connect().
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

bool connect();

bool setColor(uint8_t r, uint8_t g, uint8_t b);
bool powerOn();
bool powerOff();

// The answer arrives asynchronously; read it with lastStatus().
bool requestStatus();

// Latest status received from the lamp.
Status lastStatus();

}  // namespace LedLamp
