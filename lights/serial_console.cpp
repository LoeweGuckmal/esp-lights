#include "serial_console.h"

#include <Arduino.h>

#include "led_lamp.h"

void printSerialHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  r = Red");
  Serial.println("  g = Green");
  Serial.println("  b = Blue");
  Serial.println("  w = White");
  Serial.println("  y = Yellow");
  Serial.println("  p = Purple");
  Serial.println("  n = Power on");
  Serial.println("  f = Power off");
  Serial.println("  s = Request status");
  Serial.println("  ? = Show commands");
  Serial.println();
}

void handleSerialCommands() {
  if (!Serial.available()) return;

  char c = Serial.read();

  switch (c) {
    case 'r': LedLamp::setColor(255, 0, 0); break;
    case 'g': LedLamp::setColor(0, 255, 0); break;
    case 'b': LedLamp::setColor(0, 0, 255); break;
    case 'w': LedLamp::setColor(255, 255, 255); break;
    case 'y': LedLamp::setColor(255, 255, 0); break;
    case 'p': LedLamp::setColor(255, 0, 255); break;
    case 'n': LedLamp::powerOn(); break;
    case 'f': LedLamp::powerOff(); break;
    case 's': LedLamp::requestStatus(); break;
    case '?': printSerialHelp(); break;
    case '\r':
    case '\n':
    case ' ': break;
    default:
      Serial.println("Unknown command. Enter ? for help.");
      break;
  }
}
