#include "daylight.h"

#include <Preferences.h>
#include <math.h>

#include "alarm_clock.h"
#include "config.h"
#include "presence.h"
#include "room_light.h"
#include "weather.h"

namespace Daylight {
namespace {

// The room light's white brightness and temperature go up to 127.
constexpr int LAMP_MAX = 127;

// How often the forecast is downloaded, like open-meteo.js: every 15
// minutes while the weather is about to switch the light, less often when
// that is hours away.
constexpr uint32_t FETCH_SOON_S = 15 * 60;    // a change within 3 hours
constexpr uint32_t FETCH_LATER_S = 3 * 3600;  // within 9 hours
constexpr uint32_t FETCH_MAX_S = 6 * 3600;    // later, or none
constexpr uint32_t FETCH_RETRY_S = 15 * 60;   // after a failed download

// After the weather switched the light, it waits this long before
// switching again, so passing clouds don't make the light go on and off.
// open-meteo.js waited an hour after switching, too.
constexpr time_t WEATHER_HOLD_S = 3600;

// The log, saved in flash as one block. Change the version when the layout
// changes; a saved block with another version is ignored.
constexpr uint8_t LOG_VERSION = 1;
struct SavedLog {
  uint8_t version;
  uint8_t count;
  uint8_t next;  // where the next entry goes
  LogEntry entries[LOG_SIZE];
};

Preferences prefs;
bool isEnabled = true;
SavedLog history = {LOG_VERSION, 0, 0, {}};

Weather::Forecast forecast = {};
time_t fetchedAt = 0;
const char* fetchError = nullptr;
unsigned long lastFetchMs = 0;
uint32_t fetchDelayS = 0;  // 0 = as soon as the time is known

// What the sun and the weather said last: 1 = dark, 0 = bright, -1 =
// unknown (after the boot, or when the forecast ran out). The light is
// switched when one of them changes.
int8_t sunDark = -1;
int8_t weatherDark = -1;
time_t weatherSwitchedAt = 0;

// The switch that is running.
struct Switch {
  Step step;
  Trigger trigger;
  time_t since;  // given up DAYLIGHT_RETRY_S later
  bool presenceAsked;
  unsigned long fadeStartMs;
  bool lampSwitchedOn;
  int sentBrightness;
  int sentTemperature;
};

Switch current = {Step::Idle};
unsigned long lastConnectRequestMs = 0;
unsigned long lastTickMs = 0;

int percentToLamp(int percent) {
  return (percent * LAMP_MAX + 50) / 100;
}

uint8_t lampToPercent(int value) {
  return value < 0 ? NO_VALUE : (value * 100 + LAMP_MAX / 2) / LAMP_MAX;
}

// Whether the forecast says anything about `t`.
bool covers(time_t t) {
  return !isnan(Weather::radiationAt(forecast, t));
}

bool sunDarkAt(time_t t) {
  bool dark = true;  // before the first sunrise: night
  for (int d = 0; d < forecast.days; d++) {
    if (t >= forecast.sunrise[d]) dark = false;
    if (t >= forecast.sunset[d] - (time_t)DAYLIGHT_SUNSET_LEAD_S) dark = true;
  }
  return dark;
}

bool weatherDarkAt(time_t t) {
  return Weather::radiationAt(forecast, t) < DAYLIGHT_RADIATION_THRESHOLD;
}

Change nextSunChange(time_t t) {
  for (int d = 0; d < forecast.days; d++) {
    if (forecast.sunrise[d] > t) return {forecast.sunrise[d], false};
    time_t on = forecast.sunset[d] - (time_t)DAYLIGHT_SUNSET_LEAD_S;
    if (on > t) return {on, true};
  }
  return {0, false};
}

// Where the radiation crosses the threshold next, between two values.
Change nextWeatherChange(time_t t) {
  if (!covers(t)) return {0, false};

  bool dark = weatherDarkAt(t);
  int first = (t - forecast.start) / Weather::SLOT_S + 1;
  for (int i = first; i < forecast.count; i++) {
    float before = forecast.radiation[i - 1];
    float after = forecast.radiation[i];
    if ((after < DAYLIGHT_RADIATION_THRESHOLD) == dark) continue;

    float part = (before - DAYLIGHT_RADIATION_THRESHOLD) / (before - after);
    time_t at = forecast.start + (i - 1) * Weather::SLOT_S + (time_t)(part * Weather::SLOT_S);
    return {max(at, t + 1), !dark};
  }
  return {0, false};
}

uint32_t delayUntilNextFetch(time_t now) {
  Change next = nextWeatherChange(now);
  if (next.at == 0) return FETCH_MAX_S;
  if (next.at - now <= 3 * 3600) return FETCH_SOON_S;
  if (next.at - now <= 9 * 3600) return FETCH_LATER_S;
  return FETCH_MAX_S;
}

const char* download() {
  unsigned long startMs = millis();
  lastFetchMs = startMs;

  fetchError = Weather::fetch(forecast);
  if (fetchError) {
    fetchDelayS = FETCH_RETRY_S;
    Serial.printf("Daylight: forecast download failed: %s\n", fetchError);
    return fetchError;
  }

  time_t now = time(nullptr);
  fetchedAt = now;
  fetchDelayS = delayUntilNextFetch(now);
  Serial.printf("Daylight: forecast downloaded in %lu ms, %.0f W/m² now, next in %lu min\n",
                millis() - startMs, Weather::radiationAt(forecast, now),
                (unsigned long)(fetchDelayS / 60));
  return nullptr;
}

void addLog(Result result, uint8_t brightness, uint8_t temperature) {
  time_t now = time(nullptr);
  float radiation = Weather::radiationAt(forecast, now);

  history.entries[history.next] = {
      (uint32_t)now, current.trigger, result, brightness, temperature,
      (int16_t)(isnan(radiation) ? -1 : lroundf(radiation))};
  history.next = (history.next + 1) % LOG_SIZE;
  if (history.count < LOG_SIZE) history.count++;
  prefs.putBytes("log", &history, sizeof(history));

  Serial.printf("Daylight: %s -> %s\n", triggerName(current.trigger), resultName(result));
}

void finish(Result result, uint8_t brightness = NO_VALUE, uint8_t temperature = NO_VALUE) {
  addLog(result, brightness, temperature);
  current.step = Step::Idle;
}

// The sun or the weather changed: switch the light.
void request(Trigger trigger, time_t now) {
  bool on = switchesOn(trigger);
  if (current.step != Step::Idle && switchesOn(current.trigger) == on) {
    Serial.printf("Daylight: %s, already switching %s\n", triggerName(trigger), on ? "on" : "off");
    return;
  }

  current = {};
  current.step = on ? Step::Presence : Step::Lamp;
  current.trigger = trigger;
  current.since = now;
  Serial.printf("Daylight: %s, switching the light %s\n", triggerName(trigger), on ? "on" : "off");
}

// Called every second: notices when the sun or the weather changes.
void watch(time_t now) {
  if (!covers(now)) {
    // No forecast yet, or it ran out: start over with the next one.
    sunDark = -1;
    weatherDark = -1;
    return;
  }

  if (sunDark < 0) {
    // As it was a moment ago, so a change while the ESP was starting
    // still switches.
    time_t from = now - (time_t)DAYLIGHT_RETRY_S;
    if (!covers(from)) from = now;
    sunDark = sunDarkAt(from);
    weatherDark = weatherDarkAt(from);
  }

  int8_t sun = sunDarkAt(now);
  if (sun != sunDark) {
    sunDark = sun;
    request(sun ? Trigger::Sunset : Trigger::Sunrise, now);
  }

  int8_t weather = weatherDarkAt(now);
  if (weather != weatherDark && now - weatherSwitchedAt >= WEATHER_HOLD_S) {
    weatherDark = weather;
    weatherSwitchedAt = now;
    request(weather ? Trigger::Dark : Trigger::Bright, now);
  }
}

// True when the lamp is ready. Otherwise asks it to connect, every 10 s:
// don't wait for the retry backoff, which can be minutes.
bool connectLamp() {
  if (RoomLight::isConnected()) return true;
  if (millis() - lastConnectRequestMs > 10000) {
    lastConnectRequestMs = millis();
    RoomLight::connectNow();
  }
  return false;
}

// Like the alarm's sunrise, only shorter: from the lowest brightness and
// warmest white to DAYLIGHT_BRIGHTNESS and DAYLIGHT_TEMPERATURE. Brightness
// rises with the square, so it stays dim for longer at the start.
void fade() {
  unsigned long elapsedMs = millis() - current.fadeStartMs;
  if (!connectLamp()) {
    // Send everything again once the lamp is back.
    current.lampSwitchedOn = false;
    if (elapsedMs > (DAYLIGHT_FADE_S + DAYLIGHT_RETRY_S) * 1000UL) {
      finish(Result::LampUnreachable);
    }
    return;
  }

  float progress = DAYLIGHT_FADE_S ? elapsedMs / (DAYLIGHT_FADE_S * 1000.0f) : 1;
  progress = constrain(progress, 0.0f, 1.0f);
  int targetBrightness = max(1, percentToLamp(DAYLIGHT_BRIGHTNESS));
  int targetTemperature = percentToLamp(DAYLIGHT_TEMPERATURE);
  int brightness = 1 + lroundf((targetBrightness - 1) * progress * progress);
  int temperature = lroundf(LAMP_MAX + (targetTemperature - LAMP_MAX) * progress);

  if (!current.lampSwitchedOn) {
    // Dim and warm before switching on, so the lamp doesn't flash its last
    // setting. Sent again below in case it ignores them while off.
    RoomLight::setWhiteTemperature(temperature);
    RoomLight::setWhiteBrightness(brightness);
    if (!RoomLight::powerOn()) return;
    current.lampSwitchedOn = true;
    current.sentBrightness = -1;
    current.sentTemperature = -1;
  }

  if (temperature != current.sentTemperature && RoomLight::setWhiteTemperature(temperature)) {
    current.sentTemperature = temperature;
  }
  if (brightness != current.sentBrightness && RoomLight::setWhiteBrightness(brightness)) {
    current.sentBrightness = brightness;
  }

  if (progress >= 1 && current.sentBrightness == brightness &&
      current.sentTemperature == temperature) {
    Serial.println("Daylight: light is on.");
    current.step = Step::Idle;
  }
}

// Called every second: takes the running switch a step further.
void runSwitch(time_t now) {
  if (current.step == Step::Idle) return;
  if (current.step == Step::Fade) {
    fade();
    return;
  }

  if (AlarmClock::sunrise().running) {
    finish(Result::AlarmRunning);
    return;
  }

  // First the phone, then the lamp, so they don't compete for the radio.
  if (current.step == Step::Presence) {
    if (!current.presenceAsked) {
      current.presenceAsked = true;
      // A check that is running already (an alarm's, say) answers for us too.
      if (Presence::answer() != Presence::Answer::Checking) {
        Presence::check(Presence::Reason::Daylight);  // does nothing when off
      }
    }

    Presence::Answer answer = Presence::answer();
    if (answer == Presence::Answer::Checking) return;  // PRESENCE_SCAN_S at most
    if (answer == Presence::Answer::Away) {
      finish(Result::PhoneAway);
      return;
    }
    // Here, or presence is off or no phone is paired: on as usual.
    current.step = Step::Lamp;
  }

  if (!connectLamp()) {
    if (now - current.since > (time_t)DAYLIGHT_RETRY_S) finish(Result::LampUnreachable);
    return;
  }

  RoomLight::State state = RoomLight::readState();

  if (!switchesOn(current.trigger)) {
    if (state.status == 0) {
      finish(Result::AlreadyOff);
      return;
    }
    // What it had, for the log. Mode 3 is color, the others white.
    bool color = state.mode == 3;
    uint8_t brightness = color ? (state.colorBrightness < 0 ? NO_VALUE : state.colorBrightness)
                               : lampToPercent(state.whiteBrightness);
    uint8_t temperature = color ? NO_VALUE : lampToPercent(state.whiteTemp);
    if (RoomLight::powerOff()) finish(Result::Off, brightness, temperature);
    return;
  }

  if (state.status == 1) {
    finish(Result::AlreadyOn);
    return;
  }

  addLog(Result::On, DAYLIGHT_BRIGHTNESS, DAYLIGHT_TEMPERATURE);
  current.step = Step::Fade;
  current.fadeStartMs = millis();
  current.lampSwitchedOn = false;
  fade();
}

}  // namespace

void begin() {
  prefs.begin("daylight", false);
  isEnabled = prefs.getBool("on", true);

  SavedLog loaded;
  if (prefs.getBytesLength("log") == sizeof(loaded) &&
      prefs.getBytes("log", &loaded, sizeof(loaded)) == sizeof(loaded) &&
      loaded.version == LOG_VERSION && loaded.count <= LOG_SIZE && loaded.next < LOG_SIZE) {
    history = loaded;
  }

  Serial.printf("Daylight: %s, %d log entries\n", isEnabled ? "on" : "off", history.count);
}

void loop() {
  if (!isEnabled || millis() - lastTickMs < 1000) return;
  lastTickMs = millis();

  if (!AlarmClock::timeValid()) return;

  if (millis() - lastFetchMs >= fetchDelayS * 1000UL) download();

  time_t now = time(nullptr);
  watch(now);
  runSwitch(now);
}

bool enabled() {
  return isEnabled;
}

void setEnabled(bool on) {
  isEnabled = on;
  prefs.putBool("on", on);
  sunDark = -1;
  weatherDark = -1;
  if (on) {
    fetchDelayS = 0;  // a fresh forecast right away
  } else {
    stopSwitching();
  }
}

const char* refresh() {
  if (!AlarmClock::timeValid()) return "The time is not known yet";
  return download();
}

bool stopSwitching() {
  if (current.step == Step::Idle) return false;

  current.step = Step::Idle;
  Serial.println("Daylight: stopped, the light stays as it is.");
  return true;
}

Status status() {
  time_t now = time(nullptr);
  Status s = {};
  s.fetchedAt = fetchedAt;
  s.error = fetchError;
  uint32_t sinceFetchS = (millis() - lastFetchMs) / 1000;
  s.nextFetchS = fetchDelayS > sinceFetchS ? fetchDelayS - sinceFetchS : 0;
  s.forecastUntil = forecast.count ? forecast.start + (forecast.count - 1) * Weather::SLOT_S : 0;
  s.radiation = Weather::radiationAt(forecast, now);
  s.sunDark = sunDark;
  s.weatherDark = weatherDark;
  s.nextSun = nextSunChange(now);
  s.nextWeather = nextWeatherChange(now);
  s.step = current.step;
  s.trigger = current.trigger;
  if (current.step == Step::Fade && DAYLIGHT_FADE_S) {
    s.progress = min(100UL, (millis() - current.fadeStartMs) / (DAYLIGHT_FADE_S * 10));
  }
  return s;
}

int logCount() {
  return history.count;
}

LogEntry logEntry(int index) {
  return history.entries[(history.next - 1 - index + 2 * LOG_SIZE) % LOG_SIZE];
}

bool switchesOn(Trigger trigger) {
  return trigger == Trigger::Sunset || trigger == Trigger::Dark;
}

const char* triggerName(Trigger trigger) {
  switch (trigger) {
    case Trigger::Sunset: return "sunset";
    case Trigger::Sunrise: return "sunrise";
    case Trigger::Dark: return "dark";
    default: return "bright";
  }
}

const char* resultName(Result result) {
  switch (result) {
    case Result::On: return "on";
    case Result::Off: return "off";
    case Result::AlreadyOn: return "already_on";
    case Result::AlreadyOff: return "already_off";
    case Result::PhoneAway: return "phone_away";
    case Result::LampUnreachable: return "lamp_unreachable";
    default: return "alarm_running";
  }
}

}  // namespace Daylight
