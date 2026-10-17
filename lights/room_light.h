#pragma once

#include <Arduino.h>

// AwoX mesh bulb (room light), ported from the awoxmeshlight Python package.
// After connecting, the ESP pairs with the mesh name and password from
// secrets.h. That gives a session key, and every command is AES-encrypted
// with it. NimBLEDevice::init() must be called before begin().
namespace RoomLight {

// Values as the lamp reports them. -1 means not known yet.
struct State {
  int status;           // 1 = on, 0 = off
  int mode;             // 1/5 = white, 3 = color, 7 = transition (odd = on)
  int whiteBrightness;  // 1..127
  int whiteTemp;        // 0..127, 0 = cold
  int colorBrightness;  // 10..100
  int r, g, b;
};

// Prepares the BLE client. Does not connect yet and never blocks.
void begin();

// Call from loop(). Connects and pairs in the background and reconnects
// after a dropped connection, retrying every ROOM_LIGHT_RETRY_INTERVAL_MS.
// Pairing blocks for a moment right after a connection is made.
void loop();

// True when the lamp is connected, paired and ready for commands.
bool isConnected();

bool powerOn();
bool powerOff();
bool setColor(uint8_t r, uint8_t g, uint8_t b);
bool setColorBrightness(uint8_t brightness);  // 10..100
bool setWhiteTemperature(uint8_t temp);       // 0 (cold)..127 (warm)
bool setWhiteBrightness(uint8_t brightness);  // 1..127

// Reads a fresh status packet from the lamp when connected (blocks
// briefly) and returns the latest known state.
State readState();

}  // namespace RoomLight
