#include "status_led.h"

#include <Arduino.h>

#include "config.h"

namespace StatusLed {
namespace {

volatile bool lampDown = false;
volatile bool wifiDown = false;
int shown = 0;  // bit 0: red, bit 1: green

}  // namespace

void setLampDown(bool down) {
  lampDown = down;
}

void setWifiDown(bool down) {
  wifiDown = down;
}

void update() {
  int wanted = (lampDown ? 1 : 0) | (wifiDown ? 2 : 0);
  if (wanted == shown) return;
  shown = wanted;
  rgbLedWrite(STATUS_LED_PIN, lampDown ? 255 : 0, wifiDown ? 255 : 0, 0);
}

}  // namespace StatusLed
