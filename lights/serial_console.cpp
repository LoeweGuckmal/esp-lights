#include "serial_console.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "alarm_clock.h"
#include "led_lamp.h"
#include "presence.h"
#include "room_light.h"
#include "wifi_connection.h"

// handleSerialCommands() runs once per loop(), so the time between two
// calls is how long one pass of loop() took. Web requests wait that long.
static unsigned long lastCallMs = 0;
static unsigned long slowestLoopMs = 0;
static unsigned long slowestLoopAtMs = 0;
static bool loopStatsReset = false;

// "3 d 4 h", "4 h 12 min", "12 min 5 s" or "42 s"
static String duration(uint32_t s) {
  unsigned long v = s;
  char text[24];
  if (v >= 86400) {
    snprintf(text, sizeof(text), "%lu d %lu h", v / 86400, v % 86400 / 3600);
  } else if (v >= 3600) {
    snprintf(text, sizeof(text), "%lu h %lu min", v / 3600, v % 3600 / 60);
  } else if (v >= 60) {
    snprintf(text, sizeof(text), "%lu min %lu s", v / 60, v % 60);
  } else {
    snprintf(text, sizeof(text), "%lu s", v);
  }
  return text;
}

static const char* signalText(int rssi) {
  if (rssi >= -60) return "very good";
  if (rssi >= -67) return "good";
  if (rssi >= -75) return "okay";
  if (rssi >= -82) return "weak";
  return "very weak";
}

static const char* startReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "restart by software";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog, it hung";
    case ESP_RST_BROWNOUT: return "brownout, the power supply dipped";
    case ESP_RST_USB: return "over USB, an upload say";
    default: return "other";
  }
}

static void printHealth() {
  WifiStats w = wifiStats();
  if (w.connected) {
    int rssi = WiFi.RSSI();
    Serial.printf("Wi-Fi: %d dBm (%s), \"%s\" on channel %d, access point %s\n", rssi,
                  signalText(rssi), WiFi.SSID().c_str(), WiFi.channel(), WiFi.BSSIDstr().c_str());
    Serial.printf("  IP %s, gateway %s, connected for %s\n", WiFi.localIP().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str(), duration(w.sinceS).c_str());
  } else {
    Serial.printf("Wi-Fi: not connected, for %s\n", duration(w.sinceS).c_str());
  }

  if (w.weakestRssi < 0) {
    Serial.printf("  Weakest signal since the start: %d dBm, %s ago\n", w.weakestRssi,
                  duration(w.weakestRssiAgoS).c_str());
  }

  if (w.drops == 0) {
    Serial.println("  Never lost since the start.");
  } else {
    Serial.printf("  Lost %lu time(s) since the start, last %s ago: reason %u %s, %d dBm\n",
                  (unsigned long)w.drops, duration(w.lastDropAgoS).c_str(), w.lastDropReason,
                  WiFi.disconnectReasonName((wifi_err_reason_t)w.lastDropReason), w.lastDropRssi);
  }

  Serial.printf("Running for %s, started by: %s\n",
                duration(esp_timer_get_time() / 1000000).c_str(), startReason());
  Serial.printf("Memory: %lu KB free, lowest %lu KB since the start, largest block %lu KB\n",
                (unsigned long)ESP.getFreeHeap() / 1024, (unsigned long)ESP.getMinFreeHeap() / 1024,
                (unsigned long)ESP.getMaxAllocHeap() / 1024);
  Serial.printf("Slowest loop pass since %s: %lu ms, %s ago\n",
                loopStatsReset ? "the last W" : "the start", slowestLoopMs,
                duration((millis() - slowestLoopAtMs) / 1000).c_str());
  slowestLoopMs = 0;
  loopStatsReset = true;
}

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
  Serial.println("Wi-Fi:");
  Serial.println("  W = Signal in dBm, drops, memory, uptime");
  Serial.println("Presence:");
  Serial.println("  P = Look for the phone now");
  Serial.println("  ? = Show commands");
  Serial.println();
}

void handleSerialCommands() {
  unsigned long now = millis();
  if (lastCallMs != 0 && now - lastCallMs > slowestLoopMs) {
    slowestLoopMs = now - lastCallMs;
    slowestLoopAtMs = now;
  }
  lastCallMs = now;

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
    case 'W': printHealth(); break;
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
