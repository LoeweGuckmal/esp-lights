#include "wifi_connection.h"

#include <WiFi.h>

#include "config.h"
#include "secrets.h"

namespace {

// How often maintainWifi() reads the signal strength.
constexpr unsigned long RSSI_SAMPLE_MS = 10000;

// Written from the network event task by onWifiEvent(), read in loop().
volatile bool online = false;  // connected, with an IP address
volatile unsigned long onlineSinceMs = 0;
volatile uint32_t drops = 0;
volatile uint8_t lastDropReason = 0;
volatile int8_t lastDropRssi = 0;
volatile unsigned long lastDropMs = 0;

int weakestRssi = 0;
unsigned long weakestRssiMs = 0;
unsigned long lastSampleMs = 0;

// The reconnect in maintainWifi()
bool offline = false;
unsigned long offlineSinceMs = 0;
unsigned long lastRetryMs = 0;

// Runs in the network event task.
void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    if (online) return;  // the same connection, a renewed address
    online = true;
    onlineSinceMs = millis();
    if (drops > 0) {
      Serial.printf("Wi-Fi: connected again after %lu s, IP %s\n",
                    (millis() - lastDropMs) / 1000, WiFi.localIP().toString().c_str());
    }
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    // While reconnecting, every failed attempt ends up here too.
    if (!online) return;
    online = false;
    drops = drops + 1;
    lastDropMs = millis();
    lastDropReason = info.wifi_sta_disconnected.reason;
    lastDropRssi = info.wifi_sta_disconnected.rssi;
    Serial.printf("Wi-Fi: connection lost, reason %u %s, signal %d dBm\n", lastDropReason,
                  WiFi.disconnectReasonName((wifi_err_reason_t)lastDropReason), lastDropRssi);
  }
}

}  // namespace

void connectWifi() {
  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  WiFi.setSleep(true);
  Serial.println();
  Serial.print("ESP32 IP address: ");
  Serial.println(WiFi.localIP());
}

void maintainWifi() {
  unsigned long now = millis();
  bool connected = WiFi.isConnected();

  if (connected && now - lastSampleMs >= RSSI_SAMPLE_MS) {
    lastSampleMs = now;
    int rssi = WiFi.RSSI();  // 0 = no reading
    if (rssi < 0 && (weakestRssi == 0 || rssi < weakestRssi)) {
      weakestRssi = rssi;
      weakestRssiMs = now;
    }
  }

  if (connected) {
    offline = false;
    return;
  }

  // The ESP32 core reconnects by itself after most drops, so give it time
  // first. After some (a router that is just starting and turns the ESP
  // away, say) it gives up for good.
  if (!offline) {
    offline = true;
    offlineSinceMs = now;
    lastRetryMs = now;
    return;
  }
  if (now - lastRetryMs < WIFI_RECONNECT_S * 1000UL) return;

  lastRetryMs = now;
  Serial.printf("Wi-Fi: offline for %lu s, connecting again...\n", (now - offlineSinceMs) / 1000);
  WiFi.reconnect();
}

WifiStats wifiStats() {
  unsigned long now = millis();
  WifiStats s = {};
  s.connected = WiFi.isConnected();
  s.sinceS = (now - (s.connected ? onlineSinceMs : lastDropMs)) / 1000;
  s.drops = drops;
  s.lastDropReason = lastDropReason;
  s.lastDropRssi = lastDropRssi;
  s.lastDropAgoS = (now - lastDropMs) / 1000;
  s.weakestRssi = weakestRssi;
  s.weakestRssiAgoS = (now - weakestRssiMs) / 1000;
  return s;
}
