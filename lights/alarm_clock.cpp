#include "alarm_clock.h"

#include <Preferences.h>

#include "config.h"
#include "presence.h"
#include "room_light.h"

namespace AlarmClock {
namespace {

// Alarms are planned this many days ahead (plus today).
constexpr int DAYS_AHEAD = 7;

// The room light's white brightness and temperature go up to 127.
constexpr int LAMP_MAX = 127;

// The alarm on one day, changed or skipped for that day only.
struct NextChange {
  uint32_t date;  // yyyymmdd, 0 = no change
  bool skip;
  uint8_t hour;
  uint8_t minute;
};

// Saved in flash as one block. Change the version when the layout changes;
// a saved block with another version is ignored.
constexpr uint8_t SAVED_VERSION = 1;
struct Saved {
  uint8_t version;
  bool enabled;
  uint8_t count;
  Schedule schedules[MAX_SCHEDULES];
  NextChange next;
};

Saved data = {SAVED_VERSION, true, 0, {}, {0, false, 0, 0}};

// An alarm that rings once at a fixed time, switching the light on at once.
struct Once {
  int64_t at;  // 0 = not set
  uint8_t brightness;
  uint8_t temperature;
};

// Saved apart from `data`, so the schedules don't need a new version.
constexpr uint8_t ONCE_VERSION = 1;
struct SavedOnce {
  uint8_t version;
  Once oneTime;
  Once snooze;
};

SavedOnce once = {ONCE_VERSION, {0, 0, 0}, {0, 0, 0}};
Preferences prefs;

// Every alarm up to this time has rung (or was missed on purpose).
// Set to the current time once the time is known, so alarms from before a
// reboot or a settings change don't ring late.
time_t doneUntil = 0;

unsigned long lastTickMs = 0;

// The alarm that rang last (at = 0: none since the boot), for the snooze.
Alarm lastRang = {};

// The alarm whose sunrise is running, see keyOf(). 0 = none.
uint32_t sunriseKey = 0;
bool lampSwitchedOn = false;
int sentBrightness = -1;
int sentTemperature = -1;
unsigned long lastConnectRequestMs = 0;

// The alarm (by its time) whose presence check was started, and the one
// whose answer is in. Each alarm asks once.
time_t presenceCheckedFor = 0;
time_t presenceDecidedFor = 0;

void save() {
  prefs.putBytes("data", &data, sizeof(data));
  prefs.putBytes("once", &once, sizeof(once));
}

// After every change by the user: alarms that are already in the past
// (like a new schedule for earlier today) must not ring now.
void changed() {
  if (timeValid()) doneUntil = max(doneUntil, time(nullptr));
  save();
}

// Local noon, `daysAhead` days from today. Noon keeps the date right
// across daylight saving changes.
tm dayFromToday(int daysAhead) {
  time_t now = time(nullptr);
  tm day;
  localtime_r(&now, &day);
  day.tm_mday += daysAhead;
  day.tm_hour = 12;
  day.tm_min = 0;
  day.tm_sec = 0;
  day.tm_isdst = -1;
  mktime(&day);  // normalizes the date and fills in tm_wday
  return day;
}

time_t timeOnDay(tm day, int hour, int minute) {
  day.tm_hour = hour;
  day.tm_min = minute;
  day.tm_sec = 0;
  day.tm_isdst = -1;
  return mktime(&day);
}

// Finds `date` within the next DAYS_AHEAD days.
bool findDay(uint32_t date, tm& out) {
  for (int i = 0; i <= DAYS_AHEAD; i++) {
    tm day = dayFromToday(i);
    if (dateOf(day) == date) {
      out = day;
      return true;
    }
  }
  return false;
}

// The enabled schedule for this weekday, or -1.
int scheduleFor(int weekday) {
  for (int i = 0; i < data.count; i++) {
    const Schedule& s = data.schedules[i];
    if (s.enabled && (s.days & (1 << weekday))) return i;
  }
  return -1;
}

// Sort key: position of the first day in a week starting on Monday.
int firstDay(uint8_t days) {
  for (int i = 0; i < 7; i++) {
    if (days & (1 << ((i + 1) % 7))) return i;
  }
  return 7;
}

int percentToLamp(int percent) {
  return (percent * LAMP_MAX + 50) / 100;
}

Alarm onceAlarm(const Once& o, Kind kind) {
  Alarm alarm = {};
  alarm.at = (time_t)o.at;
  tm local;
  localtime_r(&alarm.at, &local);
  alarm.date = dateOf(local);
  alarm.weekday = local.tm_wday;
  alarm.hour = local.tm_hour;
  alarm.minute = local.tm_min;
  alarm.kind = kind;
  alarm.scheduleIndex = -1;
  alarm.fadeMinutes = 0;  // switches on at once
  alarm.brightness = o.brightness;
  alarm.temperature = o.temperature;
  return alarm;
}

// Identifies an alarm while its time can still be changed: by date and kind.
uint32_t keyOf(const Alarm& alarm) {
  return alarm.date * 4 + (uint32_t)alarm.kind;
}

// The next scheduled alarm, skipped or not as asked.
bool nextScheduled(Alarm& out, bool allowSkipped) {
  Alarm list[4];  // the one-time alarm and the snooze can come first
  int count = upcomingAlarms(list, 4);
  for (int i = 0; i < count; i++) {
    if (list[i].kind == Kind::Schedule && (allowSkipped || !list[i].skipped)) {
      out = list[i];
      return true;
    }
  }
  return false;
}

// The next alarm that rings (not skipped).
bool nextAlarm(Alarm& out) {
  Alarm list[2];  // at most one of them is skipped
  int count = upcomingAlarms(list, 2);
  for (int i = 0; i < count; i++) {
    if (!list[i].skipped) {
      out = list[i];
      return true;
    }
  }
  return false;
}

time_t sunriseStart(const Alarm& alarm) {
  return alarm.at - alarm.fadeMinutes * 60;
}

// 0 at the start of the sunrise, 1 at the alarm time.
float progressAt(const Alarm& alarm, time_t now) {
  if (alarm.fadeMinutes == 0) return 1;
  float progress = (float)(now - sunriseStart(alarm)) / (alarm.fadeMinutes * 60);
  return constrain(progress, 0.0f, 1.0f);
}

void finish(const Alarm& alarm) {
  doneUntil = max(doneUntil, alarm.at);
  sunriseKey = 0;
  lastRang = alarm;
}

// Like finish(), but the alarm didn't ring, so there's nothing to snooze.
void skipRinging(const Alarm& alarm) {
  doneUntil = max(doneUntil, alarm.at);
  sunriseKey = 0;
}

// Looks for the phone before the alarm (see presence.h). Returns true while
// the answer is not in yet or when the phone isn't here: the alarm must not
// go on then. Off, or no phone paired: the alarm rings as usual.
bool waitOrSkipForPresence(const Alarm& alarm) {
  if (presenceDecidedFor == alarm.at) return false;

  if (presenceCheckedFor != alarm.at) {
    presenceCheckedFor = alarm.at;
    Presence::check(Presence::Reason::Alarm);  // does nothing when off
  }

  Presence::Answer answer = Presence::answer();
  if (answer == Presence::Answer::Checking) return true;  // PRESENCE_SCAN_S at most

  presenceDecidedFor = alarm.at;
  if (answer != Presence::Answer::Away) return false;

  Serial.println("Alarm: phone not found, so it doesn't ring.");
  skipRinging(alarm);
  return true;
}

// Drops what is not needed any more: the change for a day that is over, and
// a one-time alarm or snooze that rang (or was missed while the ESP was off).
void dropOldChanges() {
  bool dirty = false;
  if (data.next.date != 0 && data.next.date < dateOf(dayFromToday(0))) {
    data.next = {0, false, 0, 0};
    dirty = true;
  }
  if (once.oneTime.at != 0 && once.oneTime.at <= doneUntil) {
    once.oneTime = {0, 0, 0};
    dirty = true;
  }
  if (once.snooze.at != 0 && once.snooze.at <= doneUntil) {
    once.snooze = {0, 0, 0};
    dirty = true;
  }
  if (dirty) save();
}

// Called every second: starts, runs and ends the sunrise.
void runAlarm(time_t now) {
  Alarm alarm;
  if (!nextAlarm(alarm)) {
    sunriseKey = 0;
    return;
  }

  time_t start = sunriseStart(alarm);
  if (now < start - (time_t)max(ALARM_PRECONNECT_S, PRESENCE_CHECK_LEAD_S)) {
    sunriseKey = 0;
    return;
  }

  if (now > alarm.at + (time_t)ALARM_RETRY_S) {
    Serial.println("Alarm: could not reach the room light, giving up.");
    finish(alarm);
    return;
  }

  // First the phone, then the lamp, so they don't compete for the radio.
  if (waitOrSkipForPresence(alarm)) return;

  if (now < start - (time_t)ALARM_PRECONNECT_S) {
    sunriseKey = 0;
    return;
  }

  if (!RoomLight::isConnected()) {
    // Send everything again once the lamp is back.
    lampSwitchedOn = false;
    sentBrightness = -1;
    sentTemperature = -1;

    // Don't wait for the retry backoff, which can be minutes.
    if (millis() - lastConnectRequestMs > 10000) {
      lastConnectRequestMs = millis();
      RoomLight::connectNow();
    }
    return;
  }

  if (now < start) return;

  if (sunriseKey != keyOf(alarm)) {
    sunriseKey = keyOf(alarm);
    lampSwitchedOn = false;
    sentBrightness = -1;
    sentTemperature = -1;

    // Someone is up already: leave the light as it is.
    if (RoomLight::readState().status == 1) {
      Serial.println("Alarm: room light is already on, leaving it as it is.");
      finish(alarm);
      return;
    }
    Serial.printf(alarm.fadeMinutes ? "Alarm: sunrise until %02d:%02d\n"
                                    : "Alarm: light on (%02d:%02d)\n",
                  alarm.hour, alarm.minute);
  }

  // From the lowest brightness and warmest white to the alarm's setting.
  // Brightness rises with the square, so it stays dim for longer at the
  // start, like a real sunrise.
  float progress = progressAt(alarm, now);
  int targetBrightness = max(1, percentToLamp(alarm.brightness));
  int targetTemperature = percentToLamp(alarm.temperature);
  int brightness = 1 + lroundf((targetBrightness - 1) * progress * progress);
  int temperature = lroundf(LAMP_MAX + (targetTemperature - LAMP_MAX) * progress);

  if (!lampSwitchedOn) {
    // Dim and warm before switching on, so the lamp doesn't flash its last
    // setting. Sent again below in case it ignores them while off.
    RoomLight::setWhiteTemperature(temperature);
    RoomLight::setWhiteBrightness(brightness);
    if (!RoomLight::powerOn()) return;
    lampSwitchedOn = true;
    sentBrightness = -1;
    sentTemperature = -1;
  }

  if (temperature != sentTemperature && RoomLight::setWhiteTemperature(temperature)) {
    sentTemperature = temperature;
  }
  if (brightness != sentBrightness && RoomLight::setWhiteBrightness(brightness)) {
    sentBrightness = brightness;
  }

  if (progress >= 1 && sentBrightness == brightness && sentTemperature == temperature) {
    Serial.println("Alarm: good morning!");
    finish(alarm);
  }
}

}  // namespace

void begin() {
  prefs.begin("alarms", false);

  Saved loaded;
  if (prefs.getBytesLength("data") == sizeof(loaded) &&
      prefs.getBytes("data", &loaded, sizeof(loaded)) == sizeof(loaded) &&
      loaded.version == SAVED_VERSION && loaded.count <= MAX_SCHEDULES) {
    data = loaded;
  }

  SavedOnce loadedOnce;
  if (prefs.getBytesLength("once") == sizeof(loadedOnce) &&
      prefs.getBytes("once", &loadedOnce, sizeof(loadedOnce)) == sizeof(loadedOnce) &&
      loadedOnce.version == ONCE_VERSION) {
    once = loadedOnce;
  }

  configTzTime(TIME_ZONE, NTP_SERVER_1, NTP_SERVER_2);
  Serial.printf("Alarms: %d schedule(s), %s. Waiting for the time...\n",
                data.count, data.enabled ? "on" : "off");
}

void loop() {
  if (millis() - lastTickMs < 1000) return;
  lastTickMs = millis();

  if (!timeValid()) return;

  time_t now = time(nullptr);
  if (doneUntil == 0) {
    doneUntil = now;
    printStatus();
  }

  dropOldChanges();
  runAlarm(now);
}

bool timeValid() {
  return time(nullptr) > 1700000000;  // NTP has answered (later than 2023)
}

uint32_t dateOf(const tm& local) {
  return (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
}

bool enabled() {
  return data.enabled;
}

void setEnabled(bool on) {
  data.enabled = on;
  changed();
}

int scheduleCount() {
  return data.count;
}

Schedule schedule(int index) {
  return data.schedules[index];
}

const char* saveSchedule(int index, const Schedule& schedule) {
  if (index >= data.count) return "Unknown schedule";
  if (schedule.days == 0 || schedule.days > 0x7F) return "Pick at least one day";
  if (schedule.hour > 23 || schedule.minute > 59) return "Invalid time";

  // Each day belongs to one schedule only, so there are never more than 7.
  Schedule list[MAX_SCHEDULES];
  int count = 0;
  for (int i = 0; i < data.count; i++) {
    if (i == index) continue;
    Schedule other = data.schedules[i];
    other.days &= ~schedule.days;
    if (other.days && count < MAX_SCHEDULES - 1) list[count++] = other;
  }
  list[count++] = schedule;

  // Insertion sort by first day
  for (int i = 1; i < count; i++) {
    Schedule s = list[i];
    int j = i - 1;
    while (j >= 0 && firstDay(list[j].days) > firstDay(s.days)) {
      list[j + 1] = list[j];
      j--;
    }
    list[j + 1] = s;
  }

  memcpy(data.schedules, list, sizeof(Schedule) * count);
  data.count = count;
  changed();
  return nullptr;
}

const char* deleteSchedule(int index) {
  if (index < 0 || index >= data.count) return "Unknown schedule";

  for (int i = index; i < data.count - 1; i++) {
    data.schedules[i] = data.schedules[i + 1];
  }
  data.count--;
  changed();
  return nullptr;
}

int upcomingAlarms(Alarm* out, int max) {
  if (!timeValid() || !data.enabled) return 0;

  time_t now = time(nullptr);
  time_t done = doneUntil ? doneUntil : now;
  const NextChange& change = data.next;
  Alarm list[DAYS_AHEAD + 3];
  int count = 0;

  // From the schedules: one alarm per day at most.
  for (int i = 0; i <= DAYS_AHEAD; i++) {
    tm day = dayFromToday(i);
    uint32_t date = dateOf(day);
    int index = scheduleFor(day.tm_wday);
    bool isChanged = change.date == date;

    if (index < 0 && (!isChanged || change.skip)) continue;

    Alarm alarm;
    alarm.date = date;
    alarm.weekday = day.tm_wday;
    alarm.kind = Kind::Schedule;
    alarm.scheduleIndex = index;
    alarm.skipped = isChanged && change.skip;

    if (index >= 0) {
      const Schedule& s = data.schedules[index];
      alarm.hour = s.hour;
      alarm.minute = s.minute;
      alarm.fadeMinutes = s.fadeMinutes;
      alarm.brightness = s.brightness;
      alarm.temperature = s.temperature;
    } else {
      alarm.fadeMinutes = DEFAULT_FADE_MINUTES;
      alarm.brightness = DEFAULT_BRIGHTNESS;
      alarm.temperature = DEFAULT_TEMPERATURE;
    }

    alarm.changed = false;
    if (isChanged && !change.skip) {
      alarm.changed = index < 0 || alarm.hour != change.hour || alarm.minute != change.minute;
      alarm.hour = change.hour;
      alarm.minute = change.minute;
    }

    alarm.at = timeOnDay(day, alarm.hour, alarm.minute);

    // A skipped alarm stays visible until its time, so it can be undone.
    bool ahead = alarm.skipped ? alarm.at > now : alarm.at > done;
    if (ahead) list[count++] = alarm;
  }

  if (once.oneTime.at > done) list[count++] = onceAlarm(once.oneTime, Kind::OneTime);
  if (once.snooze.at > done) list[count++] = onceAlarm(once.snooze, Kind::Snooze);

  // Insertion sort by time: the one-time alarm and the snooze go in between.
  for (int i = 1; i < count; i++) {
    Alarm alarm = list[i];
    int j = i - 1;
    while (j >= 0 && list[j].at > alarm.at) {
      list[j + 1] = list[j];
      j--;
    }
    list[j + 1] = alarm;
  }

  int found = count < max ? count : max;
  for (int i = 0; i < found; i++) out[i] = list[i];
  return found;
}

const char* changeNext(uint32_t date, uint8_t hour, uint8_t minute) {
  if (!timeValid()) return "The time is not known yet";
  if (hour > 23 || minute > 59) return "Invalid time";

  time_t now = time(nullptr);

  if (date == 0) {
    Alarm alarm;
    if (!nextScheduled(alarm, true)) return "There is no scheduled alarm to change";
    date = alarm.date;
  }

  tm day;
  if (!findDay(date, day)) return "Pick a day within the next week";
  if (timeOnDay(day, hour, minute) <= now) return "That time has already passed";

  int index = scheduleFor(day.tm_wday);
  if (index < 0) return "There is no scheduled alarm on that day";
  if (data.schedules[index].hour == hour &&
      data.schedules[index].minute == minute) {
    data.next = {0, false, 0, 0};  // same as the schedule
  } else {
    data.next = {date, false, hour, minute};
  }
  changed();
  return nullptr;
}

const char* skipNext(uint32_t date) {
  if (!timeValid()) return "The time is not known yet";

  if (date == 0) {
    Alarm alarm;
    if (!nextScheduled(alarm, false)) return "There is no alarm to skip";
    date = alarm.date;
  }

  tm day;
  if (!findDay(date, day)) return "Pick a day within the next week";

  int index = scheduleFor(day.tm_wday);
  if (index < 0) {
    // Skipping a one-time alarm removes it.
    if (data.next.date != date) return "There is no alarm on that day";
    resetNext();
    return nullptr;
  }

  const Schedule& s = data.schedules[index];
  if (timeOnDay(day, s.hour, s.minute) <= time(nullptr)) {
    return "That alarm has already passed";
  }

  data.next = {date, true, s.hour, s.minute};
  changed();
  return nullptr;
}

void resetNext() {
  data.next = {0, false, 0, 0};
  changed();
}

const char* setOneTime(uint8_t hour, uint8_t minute) {
  if (!timeValid()) return "The time is not known yet";
  if (hour > 23 || minute > 59) return "Invalid time";

  time_t at = timeOnDay(dayFromToday(0), hour, minute);
  if (at <= time(nullptr)) at = timeOnDay(dayFromToday(1), hour, minute);

  once.oneTime = {(int64_t)at, DEFAULT_BRIGHTNESS, DEFAULT_TEMPERATURE};
  changed();
  return nullptr;
}

void removeOneTime() {
  once.oneTime = {0, 0, 0};
  changed();
}

bool oneTime(Alarm& out) {
  if (once.oneTime.at == 0) return false;
  out = onceAlarm(once.oneTime, Kind::OneTime);
  return true;
}

const char* snooze(int minutes) {
  if (!timeValid()) return "The time is not known yet";
  if (minutes < 1 || minutes > 120) return "Snooze 1 to 120 minutes";

  stopSunrise();

  // Comes back like the alarm that rang (lastRang), on the full minute.
  time_t at = (time(nullptr) + minutes * 60) / 60 * 60;
  bool rang = lastRang.at != 0;
  once.snooze = {(int64_t)at, rang ? lastRang.brightness : DEFAULT_BRIGHTNESS,
                 rang ? lastRang.temperature : DEFAULT_TEMPERATURE};
  changed();
  Serial.printf("Alarm: snoozed for %d min\n", minutes);
  return nullptr;
}

void cancelSnooze() {
  once.snooze = {0, 0, 0};
  changed();
}

bool snoozed(Alarm& out) {
  if (once.snooze.at == 0) return false;
  out = onceAlarm(once.snooze, Kind::Snooze);
  return true;
}

bool justRang(Alarm& out) {
  if (lastRang.at == 0 || once.snooze.at != 0) return false;
  if (time(nullptr) - lastRang.at > (time_t)ALARM_SNOOZE_OFFER_S) return false;
  out = lastRang;
  return true;
}

Sunrise sunrise() {
  Sunrise result = {};
  Alarm alarm;
  if (!nextAlarm(alarm)) return result;

  time_t now = time(nullptr);
  if (now < sunriseStart(alarm)) return result;

  result.running = true;
  result.progress = lroundf(progressAt(alarm, now) * 100);
  result.alarm = alarm;
  return result;
}

bool stopSunrise() {
  Sunrise current = sunrise();
  if (!current.running) return false;

  finish(current.alarm);
  Serial.println("Alarm: sunrise stopped.");
  return true;
}

void printStatus() {
  if (!timeValid()) {
    Serial.println("Alarms: waiting for the time from the internet.");
    return;
  }

  time_t now = time(nullptr);
  tm local;
  localtime_r(&now, &local);
  char text[40];
  strftime(text, sizeof(text), "%a %d %b %Y %H:%M:%S %Z", &local);
  Serial.printf("Time: %s\n", text);

  Alarm alarm;
  if (!data.enabled) {
    Serial.println("Alarms are off.");
  } else if (nextAlarm(alarm)) {
    Serial.printf("Next alarm: %lu %02d:%02d, sunrise %d min\n",
                  (unsigned long)alarm.date, alarm.hour, alarm.minute, alarm.fadeMinutes);
  } else {
    Serial.println("No alarm in the next week.");
  }
}

}  // namespace AlarmClock
