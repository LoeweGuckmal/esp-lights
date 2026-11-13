#pragma once

#include <stdint.h>

// Connects to the Wi-Fi from secrets.h. Blocks until connected.
void connectWifi();

// Call from loop(). Notes the signal strength now and then, and connects
// again when the Wi-Fi is still gone after WIFI_RECONNECT_S. Runs a switch
// asked for with requestWifiSwitch(); its scan blocks for about 2 s.
void maintainWifi();

// "very good", "good", "okay", "weak" or "very weak"
const char* wifiSignalText(int rssi);

// A switch to another access point of the network, see requestWifiSwitch().
enum class WifiSwitch : uint8_t {
  None,        // none since the start
  Pending,     // asked for, starts in a moment
  Connecting,  // to the other access point
  Switched,
  NoOther,     // no other access point in reach: stayed
  ScanFailed,  // stayed
  FellBack,    // the other one didn't take the ESP within
               // WIFI_SWITCH_TIMEOUT_S: back to the strongest
};

const char* wifiSwitchName(WifiSwitch state);  // "switched", "no_other", ...

// Moves to the strongest *other* access point with the network's name, for
// when the current one is slow. It starts a moment later from
// maintainWifi(), so an HTTP answer still gets out, and takes a few
// seconds; the result is in wifiStats(). The ESP then stays with that
// access point until it can't be reached for WIFI_RECONNECT_S or the ESP
// restarts; after that it takes the strongest again.
// Returns false if a switch is already running.
bool requestWifiSwitch();

// For the serial console and /wifi/status. Times are from millis(), so they only make sense
// for up to 49 days.
struct WifiStats {
  bool connected;
  uint32_t sinceS;          // connected for this long, or offline
  uint32_t drops;           // connections lost since the start
  uint8_t lastDropReason;   // Wi-Fi reason code, see WiFi.disconnectReasonName()
  int8_t lastDropRssi;      // signal just before, in dBm; 0 = unknown
  uint32_t lastDropAgoS;
  int8_t weakestRssi;       // weakest signal since the start, in dBm; 0 = none yet
  uint32_t weakestRssiAgoS;
  bool pinned;              // held to the access point of the last switch
  WifiSwitch lastSwitch;
  uint32_t lastSwitchAgoS;  // since it was asked for, began to connect or ended
};

WifiStats wifiStats();
