#include "wifi_connection.h"

#include <WiFi.h>

#include "config.h"
#include "secrets.h"

namespace {

// How often maintainWifi() reads the signal strength.
constexpr unsigned long RSSI_SAMPLE_MS = 10000;

// A switch starts this long after it was asked for, so the HTTP answer
// still gets out over the old connection.
constexpr unsigned long SWITCH_DELAY_MS = 500;
// The scan for the other access points listens this long on each channel.
// The loop stands still for about 2 s meanwhile.
constexpr uint32_t SWITCH_SCAN_MS = 150;
// The scan on WIFI_PREFERRED_CHANNEL at the start: one channel only, so
// it can listen for longer.
constexpr uint32_t PREFERRED_SCAN_MS = 500;

// Written from the network event task by onWifiEvent(), read in loop().
volatile bool online = false;  // connected, with an IP address
volatile unsigned long onlineSinceMs = 0;
volatile uint32_t drops = 0;
volatile uint8_t lastDropReason = 0;
volatile int8_t lastDropRssi = 0;
volatile unsigned long lastDropMs = 0;
volatile unsigned long wentOfflineMs = 0;
volatile bool wasOnline = false;
// Set before we leave an access point ourselves, so it isn't counted as a
// lost connection.
volatile bool leaving = false;

int weakestRssi = 0;
unsigned long weakestRssiMs = 0;
unsigned long lastSampleMs = 0;

// The reconnect in maintainWifi()
bool offline = false;
unsigned long offlineSinceMs = 0;
unsigned long lastRetryMs = 0;

// The switch to another access point. The state also keeps how the last
// one ended.
WifiSwitch switchState = WifiSwitch::None;
unsigned long switchMs = 0;  // asked for, connecting since, or ended
uint8_t switchTarget[6] = {};
// Connected to one access point by its address. Undone when it can't be
// reached, and by a restart.
bool pinned = false;

// Runs in the network event task.
void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    if (online) return;  // the same connection, a renewed address
    online = true;
    onlineSinceMs = millis();
    if (wasOnline) {
      Serial.printf("Wi-Fi: connected again after %lu s, access point %s, channel %d, IP %s\n",
                    (millis() - wentOfflineMs) / 1000, WiFi.BSSIDstr().c_str(),
                    (int)WiFi.channel(), WiFi.localIP().toString().c_str());
    }
    wasOnline = true;
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    // While reconnecting, every failed attempt ends up here too.
    if (!online) return;
    online = false;
    wentOfflineMs = millis();
    if (leaving) {
      leaving = false;
      return;
    }
    drops = drops + 1;
    lastDropMs = millis();
    lastDropReason = info.wifi_sta_disconnected.reason;
    lastDropRssi = info.wifi_sta_disconnected.rssi;
    Serial.printf("Wi-Fi: connection lost, reason %u %s, signal %d dBm\n", lastDropReason,
                  WiFi.disconnectReasonName((wifi_err_reason_t)lastDropReason), lastDropRssi);
  }
}

void endSwitch(WifiSwitch result) {
  switchState = result;
  switchMs = millis();
}

// Connects to the strongest access point of the network again, whichever.
void connectToStrongest() {
  pinned = false;
  leaving = WiFi.isConnected();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// Scans for the access points of the network, on one channel or on all
// (0), and starts connecting to the strongest one, pinned. `exclude` leaves
// out the access point with that address. Blocks for the scan.
// Returns 1 when connecting, 0 if there is none, -1 if the scan failed.
int connectPinned(uint8_t onChannel, const uint8_t* exclude, uint32_t scanMs) {
  int count = WiFi.scanNetworks(false, false, false, scanMs, onChannel, WIFI_SSID);
  if (count < 0) return -1;

  int best = -1;
  for (int i = 0; i < count; i++) {
    if (exclude && memcmp(WiFi.BSSID((uint8_t)i), exclude, 6) == 0) continue;
    if (best < 0 || WiFi.RSSI((uint8_t)i) > WiFi.RSSI((uint8_t)best)) best = i;
  }
  if (best < 0) {
    WiFi.scanDelete();
    return 0;
  }

  memcpy(switchTarget, WiFi.BSSID((uint8_t)best), sizeof(switchTarget));
  int32_t channel = WiFi.channel((uint8_t)best);
  Serial.printf("Wi-Fi: connecting to access point %s, channel %d, %d dBm\n",
                WiFi.BSSIDstr((uint8_t)best).c_str(), (int)channel, (int)WiFi.RSSI((uint8_t)best));
  WiFi.scanDelete();

  pinned = true;
  leaving = WiFi.isConnected();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, channel, switchTarget);
  return 1;
}

// Moves to the strongest other access point of the network.
void startSwitch() {
  uint8_t current[6] = {};
  bool connected = WiFi.isConnected();
  if (connected) WiFi.BSSID(current);

  int result = connectPinned(0, connected ? current : nullptr, SWITCH_SCAN_MS);
  if (result < 0) {
    Serial.println("Wi-Fi: switch failed, the scan didn't work.");
    endSwitch(WifiSwitch::ScanFailed);
  } else if (result == 0) {
    Serial.println("Wi-Fi: no other access point in reach, staying.");
    endSwitch(WifiSwitch::NoOther);
  } else {
    switchState = WifiSwitch::Connecting;
    switchMs = millis();
  }
}

// True while a switch is running: the reconnect in maintainWifi() stays
// out of it.
bool runSwitch(unsigned long now) {
  if (switchState == WifiSwitch::Pending) {
    if (now - switchMs >= SWITCH_DELAY_MS) startSwitch();
    return true;
  }
  if (switchState != WifiSwitch::Connecting) return false;

  uint8_t current[6] = {};
  if (WiFi.isConnected() && memcmp(WiFi.BSSID(current), switchTarget, sizeof(current)) == 0) {
    Serial.println("Wi-Fi: switched.");
    endSwitch(WifiSwitch::Switched);
    return false;
  }
  if (now - switchMs >= WIFI_SWITCH_TIMEOUT_S * 1000UL) {
    Serial.println("Wi-Fi: the other access point didn't take us, back to the strongest.");
    connectToStrongest();
    endSwitch(WifiSwitch::FellBack);
    return false;
  }
  return true;
}

}  // namespace

const char* wifiSignalText(int rssi) {
  if (rssi >= -60) return "very good";
  if (rssi >= -67) return "good";
  if (rssi >= -75) return "okay";
  if (rssi >= -82) return "weak";
  return "very weak";
}

const char* wifiSwitchName(WifiSwitch state) {
  switch (state) {
    case WifiSwitch::Pending: return "pending";
    case WifiSwitch::Connecting: return "connecting";
    case WifiSwitch::Switched: return "switched";
    case WifiSwitch::NoOther: return "no_other";
    case WifiSwitch::ScanFailed: return "scan_failed";
    case WifiSwitch::FellBack: return "fell_back";
    default: return "none";
  }
}

bool requestWifiSwitch() {
  if (switchState == WifiSwitch::Pending || switchState == WifiSwitch::Connecting) return false;
  switchState = WifiSwitch::Pending;
  switchMs = millis();
  return true;
}

void connectWifi() {
  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_STA);
  // Look on all channels and take the strongest access point with this
  // name, also when reconnecting. The default stops at the first one it
  // finds, which can be a far repeater or mesh node.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  // Unless one channel is preferred: then the strongest access point on
  // that one. Two scans, because at the edge of its range an access point
  // doesn't show up in every scan.
  bool preferred = false;
  if (WIFI_PREFERRED_CHANNEL != 0) {
    for (int attempt = 0; attempt < 2 && !preferred; attempt++) {
      preferred = connectPinned(WIFI_PREFERRED_CHANNEL, nullptr, PREFERRED_SCAN_MS) > 0;
    }
    if (!preferred) {
      Serial.printf("Wi-Fi: no access point found on channel %d, taking the strongest.\n",
                    (int)WIFI_PREFERRED_CHANNEL);
    }
  }
  if (!preferred) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (pinned && millis() - start >= WIFI_SWITCH_TIMEOUT_S * 1000UL) {
      Serial.printf("\nWi-Fi: the access point on channel %d didn't take us, taking the strongest",
                    (int)WIFI_PREFERRED_CHANNEL);
      connectToStrongest();
    }
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

  if (runSwitch(now)) {
    offline = false;
    return;
  }

  if (WiFi.isConnected()) {
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
  if (pinned) {
    // The access point of the last switch is gone: any will do.
    connectToStrongest();
  } else {
    WiFi.reconnect();
  }
}

WifiStats wifiStats() {
  unsigned long now = millis();
  WifiStats s = {};
  s.connected = WiFi.isConnected();
  s.sinceS = (now - (s.connected ? onlineSinceMs : wentOfflineMs)) / 1000;
  s.drops = drops;
  s.lastDropReason = lastDropReason;
  s.lastDropRssi = lastDropRssi;
  s.lastDropAgoS = (now - lastDropMs) / 1000;
  s.weakestRssi = weakestRssi;
  s.weakestRssiAgoS = (now - weakestRssiMs) / 1000;
  s.pinned = pinned;
  s.lastSwitch = switchState;
  s.lastSwitchAgoS = (now - switchMs) / 1000;
  return s;
}
