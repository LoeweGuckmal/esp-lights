#pragma once

#include <Arduino.h>
#include <time.h>

// Switches the room light by daylight, like dayLightLamp() and
// monitorLightByShortwaveRadiation() in open-meteo.js on the PC:
// - by the sun: on DAYLIGHT_SUNSET_LEAD_S before sunset, off at sunrise
// - by the weather: on when the solar radiation drops below
//   DAYLIGHT_RADIATION_THRESHOLD, off when it rises above it again
// Both come from the forecast (see weather.h). The light only switches when
// the sun or the weather changes, so in between you can switch it as you
// like. It comes on with a short sunrise fade (DAYLIGHT_FADE_S), and only
// when your phone is here (see presence.h); going off doesn't ask.
// Every switch goes into a log in flash.
namespace Daylight {

// What switches the light.
enum class Trigger : uint8_t {
  Sunset,   // DAYLIGHT_SUNSET_LEAD_S before sunset: on
  Sunrise,  // off
  Dark,     // the radiation dropped below the threshold: on
  Bright,   // it rose above it: off
};

enum class Result : uint8_t {
  On,               // switched on, fades in
  Off,              // switched off
  AlreadyOn,        // left as it was
  AlreadyOff,
  PhoneAway,        // the presence check didn't hear the phone: stays off
  LampUnreachable,  // for DAYLIGHT_RETRY_S
  AlarmRunning,     // an alarm's sunrise has the light: left to the alarm
};

constexpr uint8_t NO_VALUE = 255;

// One switch in the log.
struct LogEntry {
  uint32_t at;          // Unix time
  Trigger trigger;
  Result result;
  uint8_t brightness;   // percent: what it fades to (On), what it had (Off)
  uint8_t temperature;  // percent, 0 = cold; NO_VALUE in color mode
  int16_t radiation;    // W/m² from the forecast at that time, -1 = unknown
};

constexpr int LOG_SIZE = 40;  // the oldest entries make room

// Where a running switch is.
enum class Step : uint8_t {
  Idle,      // none running
  Presence,  // looking for the phone (switching on only)
  Lamp,      // waiting for the lamp to connect
  Fade,      // fading in
};

// A coming change of the sun or the weather.
struct Change {
  time_t at;  // 0 = none in the forecast
  bool on;
};

struct Status {
  time_t fetchedAt;      // last forecast, 0 = none yet
  const char* error;     // of the last download, nullptr = it worked
  uint32_t nextFetchS;
  time_t forecastUntil;  // 0 = no forecast
  float radiation;       // now, in W/m²; NAN = unknown
  int8_t sunDark;        // what the sun and the weather say: 1 = dark,
  int8_t weatherDark;    // 0 = bright, -1 = unknown
  Change nextSun;
  Change nextWeather;
  Step step;             // the running switch
  Trigger trigger;
  int progress;          // of the fade, 0..100
};

// Loads the master switch and the log from flash.
void begin();

// Call from loop(). Downloads the forecast now and then (blocks for a
// moment then) and switches the light.
void loop();

// Master switch, saved in flash. When off, nothing is downloaded or switched.
bool enabled();
void setEnabled(bool on);

// Downloads the forecast now. Blocks for a moment.
// Returns nullptr on success, otherwise an error message.
const char* refresh();

// Ends a running switch (a fade, or one still waiting for the phone check
// or the lamp) and leaves the light as it is. Returns false if none was
// running. Manual commands for the room light call this.
bool stopSwitching();

Status status();

int logCount();
LogEntry logEntry(int index);  // 0 = newest

bool switchesOn(Trigger trigger);
const char* triggerName(Trigger trigger);  // "sunset", "sunrise", "dark", "bright"
const char* resultName(Result result);     // "on", "already_on", "phone_away", ...

}  // namespace Daylight
