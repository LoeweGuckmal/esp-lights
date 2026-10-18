#pragma once

#include <Arduino.h>
#include <time.h>

// Wake-up alarms for the room light, like the sleep schedule on the iPhone:
// weekly schedules (each weekday belongs to at most one of them) and a
// change of the next alarm only. Before the alarm time the light fades in
// like a sunrise. Alarms are saved in flash, the time comes from NTP.
namespace AlarmClock {

constexpr int MAX_SCHEDULES = 7;  // each one has at least one day

// Light settings for new schedules and one-time alarms
constexpr uint8_t DEFAULT_FADE_MINUTES = 15;
constexpr uint8_t DEFAULT_BRIGHTNESS = 100;
constexpr uint8_t DEFAULT_TEMPERATURE = 50;

struct Schedule {
  uint8_t days;         // bit 0 = Sunday .. bit 6 = Saturday (like tm_wday)
  uint8_t hour;
  uint8_t minute;
  uint8_t fadeMinutes;  // sunrise length, 0 = switch on at the alarm time
  uint8_t brightness;   // 1..100
  uint8_t temperature;  // 0 = cold .. 100 = warm
  bool enabled;
};

// One planned alarm, from a schedule or changed for that day only.
struct Alarm {
  time_t at;           // when the light reaches its full setting
  uint32_t date;       // local date as yyyymmdd
  uint8_t weekday;     // 0 = Sunday
  uint8_t hour;
  uint8_t minute;
  int scheduleIndex;   // schedule of that day, -1 = one-time alarm
  bool changed;        // time changed for this day only
  bool skipped;        // skipped for this day only, does not ring
  uint8_t fadeMinutes;
  uint8_t brightness;
  uint8_t temperature;
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

// Up to `max` alarms of the next 7 days, in order. A skipped alarm is
// included (skipped = true) until its time has passed.
int upcomingAlarms(Alarm* out, int max);

// Changes the alarm on `date` (yyyymmdd) for that day only. On a day without
// a schedule this is a one-time alarm. date 0 = the next alarm, or if there
// is none, the next time it is hour:minute.
// Only one day can be changed at a time; a new change replaces the old one.
const char* changeNext(uint32_t date, uint8_t hour, uint8_t minute);

// Skips the alarm on `date` (0 = the next alarm) once.
const char* skipNext(uint32_t date);

// Back to the schedule: undoes changeNext() or skipNext().
void resetNext();

Sunrise sunrise();

// Ends a running sunrise and leaves the light as it is. Returns false if
// none was running. Manual commands for the room light call this.
bool stopSunrise();

// Prints the time and the next alarm to the serial monitor.
void printStatus();

}  // namespace AlarmClock
