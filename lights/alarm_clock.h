#pragma once

#include <Arduino.h>
#include <time.h>

// Wake-up alarms for the room light, like the sleep schedule on the iPhone:
// weekly schedules (each weekday belongs to at most one of them) and a
// change of the next alarm only. Before the alarm time the light fades in
// like a sunrise. On top of that, a one-time alarm and a snooze, which switch
// the light on at once. Alarms are saved in flash, the time comes from NTP.
namespace AlarmClock {

constexpr int MAX_SCHEDULES = 7;  // each one has at least one day

// Light settings for new schedules and one-time alarms
constexpr uint8_t DEFAULT_FADE_MINUTES = 15;
constexpr uint8_t DEFAULT_BRIGHTNESS = 70;
constexpr uint8_t DEFAULT_TEMPERATURE = 50;

struct Schedule {
  uint8_t days;         // bit 0 = Sunday .. bit 6 = Saturday (like tm_wday)
  uint8_t hour;
  uint8_t minute;
  uint8_t fadeMinutes;  // sunrise length, 0 = switch on at the alarm time
  uint8_t brightness;   // 1..100
  uint8_t temperature;  // 0 = cold .. 100 = warm
  bool enabled;
  bool darkOnly;        // stays off when it's bright by then, see tooBright()
};

enum class Kind : uint8_t {
  Schedule,  // from a schedule, maybe changed or skipped for that day only
  OneTime,   // setOneTime()
  Snooze,    // snooze()
};

// One planned alarm.
struct Alarm {
  time_t at;           // when the light reaches its full setting
  uint32_t date;       // local date as yyyymmdd
  uint8_t weekday;     // 0 = Sunday
  uint8_t hour;
  uint8_t minute;
  Kind kind;
  int scheduleIndex;   // schedule of that day, -1 = none
  bool changed;        // time changed for this day only
  bool skipped;        // skipped for this day only, does not ring
  uint8_t fadeMinutes;
  uint8_t brightness;
  uint8_t temperature;
  bool darkOnly;       // from the schedule, or the alarm a snooze belongs to
};

struct Sunrise {
  bool running;  // fading in, or waiting for the lamp at the alarm time
  int progress;  // 0..100
  Alarm alarm;
};

// Loads the saved alarms and starts the time sync. Call after Wi-Fi is up.
void begin();

// Call from loop(). Runs the sunrise when an alarm is due.
void loop();

// False until the time has been received from the internet.
bool timeValid();

// Local date as yyyymmdd.
uint32_t dateOf(const tm& local);

// Master switch: when off, no alarm rings.
bool enabled();
void setEnabled(bool on);

int scheduleCount();
Schedule schedule(int index);

// Adds (index -1) or replaces a schedule. Its days are taken away from the
// other schedules, and schedules left without days are deleted. The list is
// sorted by the first day afterwards, so indexes can change.
// These return nullptr on success, otherwise an error message.
const char* saveSchedule(int index, const Schedule& schedule);
const char* deleteSchedule(int index);

// Up to `max` alarms of the next 7 days, in order, including the one-time
// alarm and the snooze. A skipped alarm is included (skipped = true) until
// its time has passed.
int upcomingAlarms(Alarm* out, int max);

// Changes the scheduled alarm on `date` (yyyymmdd) for that day only.
// date 0 = the next scheduled alarm.
// Only one day can be changed at a time; a new change replaces the old one.
const char* changeNext(uint32_t date, uint8_t hour, uint8_t minute);

// Skips the scheduled alarm on `date` (0 = the next one) once.
const char* skipNext(uint32_t date);

// Back to the schedule: undoes changeNext() or skipNext().
void resetNext();

// One-time alarm at the next hour:minute (today if that time is still
// ahead, else tomorrow), apart from the schedule. It switches the light on
// at once, without a sunrise. A new one replaces the old one.
const char* setOneTime(uint8_t hour, uint8_t minute);
void removeOneTime();
bool oneTime(Alarm& out);  // false if none is set

// Ends a running sunrise and rings again in `minutes` (ALARM_SNOOZE_EARLY_S
// earlier): the light comes back on at once, like the alarm that rang. An
// alarm due before then doesn't ring; the snooze takes it over. Switching
// the light off until then is up to the caller.
const char* snooze(int minutes);
void cancelSnooze();
bool snoozed(Alarm& out);  // false if no snooze is set

// Whether snooze(minutes) belongs to an alarm of the ESP: a sunrise is
// running, an alarm or snooze rang at most ALARM_PHONE_SNOOZE_S ago, a
// snooze is set, or the next alarm is due within `minutes`. If that alarm
// is dark-only, also only while it's dark (Daylight::darkNow). For a phone
// automation that runs on every snoozed alarm, also one in the afternoon.
bool alarmGoingOn(int minutes);

// Whether a dark-only alarm stays off: the forecast says it's bright at its
// time (Daylight::darkAt). Unknown counts as dark, so it rings. Decided
// once, just before its sunrise; until then this is a forecast.
bool tooBright(const Alarm& alarm);

// The alarm that rang last, if that was less than ALARM_SNOOZE_OFFER_S ago
// and no snooze is set: the page offers a snooze then.
bool justRang(Alarm& out);

Sunrise sunrise();

// Ends a running sunrise and leaves the light as it is. Returns false if
// none was running. Manual commands for the room light call this.
bool stopSunrise();

// Prints the time and the next alarm to the serial monitor.
void printStatus();

}  // namespace AlarmClock
