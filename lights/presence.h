#pragma once

#include <Arduino.h>

// Is my phone here? The ESP listens for the phone's Bluetooth (BLE)
// advertisements, but only on demand: before an alarm (see alarm_clock.cpp),
// before daylight switches the light on (see daylight.cpp) and when asked
// on the /presence page. Nothing runs in between.
//
// iPhones change their Bluetooth address about every 15 minutes. To still
// recognize yours, the ESP needs its identity key (IRK), which it gets by
// pairing once (startPairing()). The key is saved in flash.
namespace Presence {

enum class Answer : uint8_t {
  Unknown,   // off, no phone paired, or no check in the last few minutes
  Checking,  // a check is running
  Present,   // heard with at least PRESENCE_MIN_RSSI
  Away,      // not heard, or only weaker than PRESENCE_MIN_RSSI
};

// Why a check runs. Watch listens longer and doesn't stop when the phone
// is heard, so the page can show the signal strength live.
enum class Reason : uint8_t { Manual, Alarm, Watch, Daylight };

struct Result {
  bool valid;          // a check has run (or is running) since the boot
  bool running;
  Reason reason;
  bool seen;           // the phone was heard at all
  bool found;          // ... with at least PRESENCE_MIN_RSSI
  int rssi;            // latest signal strength in dBm, if seen
  int bestRssi;        // strongest one of this check, if seen
  uint32_t heardAgoS;  // since the phone was last heard, if seen
  uint32_t ageS;       // since the check ended (0 while running)
  uint32_t leftS;      // until it ends (0 when done)
};

// Loads the saved phone and registers the pairing service. Call in setup()
// after NimBLEDevice::init() and before any BLE connection: NimBLE can't add
// services while something is connected.
void begin();

// Call from loop().
void loop();

// Master switch, saved in flash. When off, the ESP never scans or
// advertises for this, and alarms ring as usual.
bool enabled();
void setEnabled(bool on);

bool hasPhone();
void forgetPhone();

// Listens for the phone, at most PRESENCE_SCAN_S (PRESENCE_WATCH_S for
// Reason::Watch). Never blocks; a running check starts over.
// Returns nullptr on success, otherwise an error message.
const char* check(Reason reason);
void stopCheck();

Answer answer();
Result result();

// For PRESENCE_PAIR_WINDOW_S, the phone can connect and pair: the ESP
// advertises as BLE_DEVICE_NAME. Ends by itself once a phone is paired.
const char* startPairing();
void stopPairing();
uint32_t pairingLeftS();  // 0 = not pairing
bool lastPairingWorked();  // the last pairing ended with a phone paired

}  // namespace Presence
