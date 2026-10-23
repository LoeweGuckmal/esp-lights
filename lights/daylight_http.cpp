#include "daylight_http.h"

#include <math.h>
#include <time.h>

#include "alarm_clock.h"
#include "config.h"
#include "daylight.h"
#include "http_utils.h"

namespace {

// Local time like "2026-10-21 17:40:12", in quotes, or null.
String timeJson(time_t t) {
  if (t == 0) return "null";
  tm local;
  localtime_r(&t, &local);
  char text[24];
  strftime(text, sizeof(text), "\"%Y-%m-%d %H:%M:%S\"", &local);
  return text;
}

String textJson(const char* text) {
  if (!text) return "null";
  return "\"" + String(text) + "\"";
}

String percentJson(uint8_t value) {
  return value == Daylight::NO_VALUE ? String("null") : String(value);
}

String darkJson(int8_t dark) {
  if (dark < 0) return "null";
  return dark ? "\"dark\"" : "\"bright\"";
}

String actionJson(bool on) {
  return on ? "\"on\"" : "\"off\"";
}

String changeJson(const Daylight::Change& change) {
  if (change.at == 0) return "null";
  return "{\"time\":" + timeJson(change.at) + ",\"action\":" + actionJson(change.on) + "}";
}

const char* stepText(Daylight::Step step) {
  switch (step) {
    case Daylight::Step::Presence: return "presence";
    case Daylight::Step::Lamp: return "lamp";
    default: return "fade";
  }
}

String statusFields() {
  Daylight::Status s = Daylight::status();

  String json = "\"enabled\":" + jsonBool(Daylight::enabled());
  json += ",\"time_valid\":" + jsonBool(AlarmClock::timeValid());
  json += ",\"radiation\":" + (isnan(s.radiation) ? String("null") : String(s.radiation, 1));
  json += ",\"sun\":" + darkJson(s.sunDark);
  json += ",\"weather\":" + darkJson(s.weatherDark);
  json += ",\"now\":" + darkJson(Daylight::darkNow());  // what if_dark=1 sees
  json += ",\"next_sun\":" + changeJson(s.nextSun);
  json += ",\"next_weather\":" + changeJson(s.nextWeather);

  if (s.step == Daylight::Step::Idle) {
    json += ",\"switching\":null";
  } else {
    json += ",\"switching\":{\"action\":" + actionJson(Daylight::switchesOn(s.trigger)) +
            ",\"trigger\":\"" + Daylight::triggerName(s.trigger) + "\"" +
            ",\"step\":\"" + stepText(s.step) + "\"" +
            ",\"progress\":" + String(s.progress) + "}";
  }

  json += ",\"forecast\":{\"downloaded\":" + timeJson(s.fetchedAt) +
          ",\"until\":" + timeJson(s.forecastUntil) +
          ",\"error\":" + textJson(s.error) +
          ",\"next_download_min\":" + String((s.nextFetchS + 59) / 60) + "}";

  // Settings from config.h
  json += ",\"settings\":{\"threshold\":" + String(DAYLIGHT_RADIATION_THRESHOLD, 0) +
          ",\"sunset_lead_min\":" + String(DAYLIGHT_SUNSET_LEAD_S / 60) +
          ",\"fade_s\":" + String(DAYLIGHT_FADE_S) +
          ",\"brightness\":" + String(DAYLIGHT_BRIGHTNESS) +
          ",\"temperature\":" + String(DAYLIGHT_TEMPERATURE) + "}";
  return json;
}

void sendStatus(WebServer& server) {
  sendOk(server, statusFields());
}

void sendLog(WebServer& server) {
  String json;
  json.reserve(Daylight::LOG_SIZE * 160);
  json = "\"log\":[";
  for (int i = 0; i < Daylight::logCount(); i++) {
    Daylight::LogEntry e = Daylight::logEntry(i);
    if (i > 0) json += ",";
    json += "{\"time\":" + timeJson(e.at) +
            ",\"trigger\":\"" + Daylight::triggerName(e.trigger) + "\"" +
            ",\"action\":" + actionJson(Daylight::switchesOn(e.trigger)) +
            ",\"result\":\"" + Daylight::resultName(e.result) + "\"" +
            ",\"brightness\":" + percentJson(e.brightness) +
            ",\"temperature\":" + percentJson(e.temperature) +
            ",\"radiation\":" + (e.radiation < 0 ? String("null") : String(e.radiation)) + "}";
  }
  json += "]";
  sendOk(server, json);
}

}  // namespace

bool checkIfDark(WebServer& server) {
  int ifDark = 0;
  if (!readIntParam(server, "if_dark", ifDark, 0, 1)) return false;
  if (ifDark == 0 || Daylight::darkNow() != 0) return true;

  sendOk(server, "\"switched\":false,\"dark\":false");
  return false;
}

void registerDaylightRoutes(WebServer& server) {
  server.on("/daylight/status", HTTP_ANY, [&server]() { sendStatus(server); });
  server.on("/daylight/log", HTTP_ANY, [&server]() { sendLog(server); });
  server.on("/daylight/on", HTTP_ANY, [&server]() {
    Daylight::setEnabled(true);
    sendStatus(server);
  });
  server.on("/daylight/off", HTTP_ANY, [&server]() {
    Daylight::setEnabled(false);
    sendStatus(server);
  });
  server.on("/daylight/refresh", HTTP_ANY, [&server]() {
    const char* error = Daylight::refresh();
    if (error) {
      sendError(server, 503, error);
      return;
    }
    sendStatus(server);
  });
}
