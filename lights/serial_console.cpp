#include "serial_console.h"

#include <Arduino.h>

#include "alarm_clock.h"
#include "led_lamp.h"
#include "presence.h"
#include "room_light.h"

static void printRoomLightState() {
  RoomLight::State s = RoomLight::readState();
  Serial.printf("Room light: connected=%d status=%d mode=%d rgb=(%d,%d,%d) "
                "color_brightness=%d white_temp=%d white_brightness=%d\n",
                RoomLight::isConnected(), s.status, s.mode, s.r, s.g, s.b,
                s.colorBrightness, s.whiteTemp, s.whiteBrightness);
}

void printSerialHelp() {
  Serial.println();
  Serial.println("LED strip:");
  Serial.println("  r = Red");
  Serial.println("  g = Green");
  Serial.println("  b = Blue");
  Serial.println("  w = White");
  Serial.println("  y = Yellow");
  Serial.println("  p = Purple");
  Serial.println("  n = Power on");
  Serial.println("  f = Power off");
  Serial.println("  s = Request status");
  Serial.println("Room light:");
  Serial.println("  N = Power on");
  Serial.println("  F = Power off");
  Serial.println("  S = Print status");
  Serial.println("Alarms:");
  Serial.println("  A = Print time and next alarm");
  Serial.println("Presence:");
  Serial.println("  P = Look for the phone now");
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
    case 'N': RoomLight::powerOn(); break;
    case 'F': RoomLight::powerOff(); break;
    case 'S': printRoomLightState(); break;
    case 'A': AlarmClock::printStatus(); break;
    case 'P': {
      const char* error = Presence::check(Presence::Reason::Manual);
      if (error) Serial.printf("Presence: %s\n", error);
      break;
    }
    case '?': printSerialHelp(); break;
    case '\r':
    case '\n':
    case ' ': break;
    default:
      Serial.println("Unknown command. Enter ? for help.");
      break;
  }
}
