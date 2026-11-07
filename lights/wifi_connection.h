#pragma once

#include <stdint.h>

// Connects to the Wi-Fi from secrets.h. Blocks until connected.
void connectWifi();

// Call from loop(). Notes the signal strength now and then, and connects
// again when the Wi-Fi is still gone after WIFI_RECONNECT_S.
void maintainWifi();

// For the serial console. Times are from millis(), so they only make sense
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
};

WifiStats wifiStats();
