#include "wifi_connection.h"

#include <WiFi.h>
#include <esp_wifi.h>

#include "config.h"
#include "secrets.h"
#include "status_led.h"

namespace {

// How often maintainWifi() reads the signal strength.
constexpr unsigned long RSSI_SAMPLE_MS = 10000;

// A switch starts this long after it was asked for, so the HTTP answer
// still gets out over the old connection.
constexpr unsigned long SWITCH_DELAY_MS = 500;
// The scan for the other access points listens this long on each channel.
// The loop stands still for about 2 s meanwhile.
constexpr uint32_t SWITCH_SCAN_MS = 150;
// The scan on WIFI_PREFERRED_CHANNEL: one channel only, so it can listen
// for longer.
constexpr uint32_t PREFERRED_SCAN_MS = 500;
// The move to the preferred channel starts this long after the start, and
// is tried again this much later if it didn't work, PREFERRED_TRIES times
// in all. Right after a restart the access point there can turn the ESP
// away for a while ("7 NOT_ASSOCED").
constexpr unsigned long PREFERRED_FIRST_MS = 10000;
constexpr unsigned long PREFERRED_RETRY_MS = 5 * 60 * 1000UL;
constexpr int PREFERRED_TRIES = 3;

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
// While connectWifi() waits and while a switch connects: dots, and every
// failed attempt with its reason in between.
volatile bool showAttempts = false;
// The reasons of the failed attempts of the running or last switch.
volatile uint8_t switchErrors[WIFI_SWITCH_ERRORS] = {};
volatile uint8_t switchErrorCount = 0;
volatile bool collectErrors = false;

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
uint8_t switchChannel = 0;  // 0 = the last switch found no access point
int8_t switchRssi = 0;
unsigned long lastDotMs = 0;
// Connected to one access point by its address. Undone when it can't be
// reached, and by a restart.
bool pinned = false;
// The running switch goes to WIFI_PREFERRED_CHANNEL, not to the other
// access point.
bool switchToPreferred = false;
int preferredTriesLeft = 0;
unsigned long nextPreferredMs = 0;

// Runs in the network event task.
void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    if (online) return;  // the same connection, a renewed address
    online = true;
    onlineSinceMs = millis();
    if (wasOnline) {
      if (showAttempts) Serial.println();
      Serial.printf("Wi-Fi: connected again after %lu s, access point %s, channel %d, IP %s\n",
                    (millis() - wentOfflineMs) / 1000, WiFi.BSSIDstr().c_str(),
                    (int)WiFi.channel(), WiFi.localIP().toString().c_str());
    }
    wasOnline = true;
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    // While reconnecting, every failed attempt ends up here too.
    if (!online) {
      uint8_t reason = info.wifi_sta_disconnected.reason;
      // 8 and 36 are our own: leaving, and ending an attempt.
      if (reason == WIFI_REASON_ASSOC_LEAVE || reason == WIFI_REASON_STA_LEAVING) return;
      if (collectErrors && switchErrorCount < WIFI_SWITCH_ERRORS) {
        switchErrors[switchErrorCount] = reason;
        switchErrorCount = switchErrorCount + 1;
      }
      if (showAttempts) {
        Serial.printf(" [%u %s]", reason, WiFi.disconnectReasonName((wifi_err_reason_t)reason));
      }
      return;
    }
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
  showAttempts = false;
  collectErrors = false;
  switchState = result;
  switchMs = millis();
  if (switchToPreferred) {
    if (result == WifiSwitch::Switched) preferredTriesLeft = 0;
    nextPreferredMs = switchMs + PREFERRED_RETRY_MS;
  }
}

// Ends a connection and a running attempt. The driver takes no new
// configuration and no scan while it is connecting ("sta is connecting,
// cannot set config"), and while an access point doesn't answer it is
// connecting nearly all the time. The core's own reconnecting is off until
// resumeReconnecting(), or it would start the old attempt again right away.
void stopConnecting() {
  WiFi.setAutoReconnect(false);
  leaving = WiFi.isConnected();
  WiFi.disconnect();
  delay(100);
}

void resumeReconnecting() {
  WiFi.setAutoReconnect(true);
}

// Connects to the strongest access point of the network again, whichever.
// Returns false if the driver didn't take it: `pinned` then stays set, and
// the callers try again.
bool connectToStrongest() {
  stopConnecting();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  resumeReconnecting();

  wifi_config_t now = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &now) != ESP_OK || now.sta.bssid_set) return false;
  pinned = false;
  return true;
}

// Scans for the access points of the network, on one channel or on all
// (0), and starts connecting to the strongest one, pinned. `exclude` leaves
// out the access point with that address. Blocks for the scan.
// Returns 1 when connecting, 0 if there is none, -1 if the scan failed.
int connectPinned(uint8_t onChannel, const uint8_t* exclude, uint32_t scanMs) {
  // A scan isn't allowed while an attempt is running. Still connected is
  // fine: the switch scans first and leaves then.
  if (!WiFi.isConnected()) stopConnecting();

  int count = WiFi.scanNetworks(false, false, false, scanMs, onChannel, WIFI_SSID);
  if (count < 0) {
    resumeReconnecting();
    return -1;
  }

  int best = -1;
  for (int i = 0; i < count; i++) {
    if (exclude && memcmp(WiFi.BSSID((uint8_t)i), exclude, 6) == 0) continue;
    if (best < 0 || WiFi.RSSI((uint8_t)i) > WiFi.RSSI((uint8_t)best)) best = i;
  }
  if (best < 0) {
    WiFi.scanDelete();
    resumeReconnecting();
    return 0;
  }

  memcpy(switchTarget, WiFi.BSSID((uint8_t)best), sizeof(switchTarget));
  int32_t channel = WiFi.channel((uint8_t)best);
  switchChannel = channel;
  switchRssi = WiFi.RSSI((uint8_t)best);
  Serial.printf("Wi-Fi: connecting to access point %s, channel %d, %d dBm",
                WiFi.BSSIDstr((uint8_t)best).c_str(), (int)channel, (int)switchRssi);
  WiFi.scanDelete();

  pinned = true;
  stopConnecting();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, channel, switchTarget);
  resumeReconnecting();
  return 1;
}

// Moves to the strongest other access point of the network, or to the
// strongest one on the preferred channel.
void startSwitch() {
  uint8_t current[6] = {};
  bool connected = WiFi.isConnected();
  if (connected) WiFi.BSSID(current);

  switchChannel = 0;
  switchErrorCount = 0;

  int result;
  if (switchToPreferred) {
    // Two scans: at the edge of its range an access point doesn't show up
    // in every one.
    result = connectPinned(WIFI_PREFERRED_CHANNEL, nullptr, PREFERRED_SCAN_MS);
    if (result == 0) result = connectPinned(WIFI_PREFERRED_CHANNEL, nullptr, PREFERRED_SCAN_MS);
  } else {
    result = connectPinned(0, connected ? current : nullptr, SWITCH_SCAN_MS);
  }

  if (result < 0) {
    Serial.println("Wi-Fi: switch failed, the scan didn't work.");
    endSwitch(WifiSwitch::ScanFailed);
  } else if (result == 0) {
    Serial.println(switchToPreferred ? "Wi-Fi: no access point found on the preferred channel, staying."
                                     : "Wi-Fi: no other access point in reach, staying.");
    endSwitch(WifiSwitch::NoOther);
  } else {
    // The line of connectPinned() goes on with dots and failed attempts.
    collectErrors = true;
    showAttempts = true;
    lastDotMs = millis();
    switchState = WifiSwitch::Connecting;
    switchMs = millis();
  }
}

// True while a switch is running: the reconnect in maintainWifi() stays
// out of it.
bool runSwitch(unsigned long now) {
  // After a start: from the strongest access point on to the preferred
  // channel, unless the ESP is there already.
  bool idle = switchState != WifiSwitch::Pending && switchState != WifiSwitch::Connecting;
  if (idle && preferredTriesLeft > 0 && (long)(now - nextPreferredMs) >= 0 && WiFi.isConnected()) {
    if (WiFi.channel() == WIFI_PREFERRED_CHANNEL) {
      preferredTriesLeft = 0;
    } else {
      preferredTriesLeft--;
      Serial.printf("Wi-Fi: on channel %d, moving to the preferred channel %d\n",
                    (int)WiFi.channel(), (int)WIFI_PREFERRED_CHANNEL);
      switchToPreferred = true;
      switchState = WifiSwitch::Pending;
      switchMs = now - SWITCH_DELAY_MS;  // no answer to wait for
    }
  }

  if (switchState == WifiSwitch::Pending) {
    if (now - switchMs >= SWITCH_DELAY_MS) startSwitch();
    return true;
  }
  if (switchState != WifiSwitch::Connecting) return false;

  uint8_t current[6] = {};
  if (WiFi.isConnected() && memcmp(WiFi.BSSID(current), switchTarget, sizeof(current)) == 0) {
    endSwitch(WifiSwitch::Switched);
    Serial.println("\nWi-Fi: switched.");
    return false;
  }
  if (now - switchMs >= WIFI_SWITCH_TIMEOUT_S * 1000UL) {
    endSwitch(WifiSwitch::FellBack);
    Serial.println("\nWi-Fi: that access point didn't take us, back to the strongest.");
    connectToStrongest();
    return false;
  }
  if (now - lastDotMs >= 500) {
    lastDotMs = now;
    Serial.print(".");
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
  // Asked for by hand: the preferred channel has no say any more.
  switchToPreferred = false;
  preferredTriesLeft = 0;
  switchState = WifiSwitch::Pending;
  switchMs = millis();
  return true;
}

void connectWifi() {
  StatusLed::setWifiDown(true);
  StatusLed::update();

  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_STA);
  // Look on all channels and take the strongest access point with this
  // name, also when reconnecting. The default stops at the first one it
  // finds, which can be a far repeater or mesh node.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  showAttempts = true;

  unsigned long lastRetry = millis();
  while (WiFi.status() != WL_CONNECTED) {
    StatusLed::update();
    delay(500);
    Serial.print(".");

    // As in maintainWifi(): don't count on the core to keep trying.
    if (millis() - lastRetry >= WIFI_RECONNECT_S * 1000UL) {
      lastRetry = millis();
      connectToStrongest();
    }
  }
  showAttempts = false;

  // The preferred channel comes a little later, from maintainWifi(): the
  // access point there may turn away an ESP that just restarted, but takes
  // one that comes over from another access point.
  if (WIFI_PREFERRED_CHANNEL != 0) {
    preferredTriesLeft = PREFERRED_TRIES;
    nextPreferredMs = millis() + PREFERRED_FIRST_MS;
  }
  StatusLed::setWifiDown(false);
  StatusLed::update();

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

  StatusLed::setWifiDown(!WiFi.isConnected());

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
  memcpy(s.switchTarget, switchTarget, sizeof(s.switchTarget));
  s.switchChannel = switchChannel;
  s.switchRssi = switchRssi;
  s.switchErrorCount = switchErrorCount;
  for (int i = 0; i < s.switchErrorCount; i++) s.switchErrors[i] = switchErrors[i];
  return s;
}
