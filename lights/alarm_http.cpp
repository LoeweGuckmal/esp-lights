#include "alarm_http.h"

#include <time.h>

#include "alarm_clock.h"
#include "alarm_page.h"
#include "http_utils.h"
#include "room_light.h"

namespace {

String timeText(int hour, int minute) {
  char text[6];
  snprintf(text, sizeof(text), "%02d:%02d", hour, minute);
  return text;
}

String scheduleJson(int id, const AlarmClock::Schedule& s) {
  return "{\"id\":" + String(id) +
         ",\"days\":" + String(s.days) +
         ",\"time\":\"" + timeText(s.hour, s.minute) + "\"" +
         ",\"enabled\":" + jsonBool(s.enabled) +
         ",\"fade\":" + String(s.fadeMinutes) +
         ",\"brightness\":" + String(s.brightness) +
         ",\"temperature\":" + String(s.temperature) + "}";
}

const char* kindText(AlarmClock::Kind kind) {
  switch (kind) {
    case AlarmClock::Kind::OneTime: return "one_time";
    case AlarmClock::Kind::Snooze: return "snooze";
    default: return "schedule";
  }
}

String alarmJson(const AlarmClock::Alarm& a, time_t now) {
  long minutes = max(0L, (long)(a.at - now + 59) / 60);
  return "{\"kind\":\"" + String(kindText(a.kind)) + "\"" +
         ",\"date\":" + String(a.date) +
         ",\"weekday\":" + String(a.weekday) +
         ",\"time\":\"" + timeText(a.hour, a.minute) + "\"" +
         ",\"schedule\":" + jsonNumber(a.scheduleIndex) +
         ",\"changed\":" + jsonBool(a.changed) +
         ",\"skipped\":" + jsonBool(a.skipped) +
         ",\"fade\":" + String(a.fadeMinutes) +
         ",\"brightness\":" + String(a.brightness) +
         ",\"temperature\":" + String(a.temperature) +
         ",\"in_minutes\":" + String(minutes) + "}";
}

// Everything the page needs in one answer.
String statusFields() {
  time_t now = time(nullptr);
  bool timeValid = AlarmClock::timeValid();

  String json = "\"time_valid\":" + jsonBool(timeValid);

  if (timeValid) {
    tm local;
    localtime_r(&now, &local);
    json += ",\"now\":{\"date\":" + String(AlarmClock::dateOf(local)) +
            ",\"weekday\":" + String(local.tm_wday) +
            ",\"time\":\"" + timeText(local.tm_hour, local.tm_min) + "\"}";
  } else {
    json += ",\"now\":null";
  }

  json += ",\"enabled\":" + jsonBool(AlarmClock::enabled());

  json += ",\"schedules\":[";
  for (int i = 0; i < AlarmClock::scheduleCount(); i++) {
    if (i > 0) json += ",";
    json += scheduleJson(i, AlarmClock::schedule(i));
  }
  json += "]";

  // Two, so the page can show what comes after a skipped alarm.
  AlarmClock::Alarm upcoming[2];
  int count = AlarmClock::upcomingAlarms(upcoming, 2);
  json += ",\"upcoming\":[";
  for (int i = 0; i < count; i++) {
    if (i > 0) json += ",";
    json += alarmJson(upcoming[i], now);
  }
  json += "]";

  AlarmClock::Sunrise sunrise = AlarmClock::sunrise();
  if (sunrise.running) {
    json += ",\"sunrise\":{\"time\":\"" +
            timeText(sunrise.alarm.hour, sunrise.alarm.minute) + "\"" +
            ",\"progress\":" + String(sunrise.progress) +
            ",\"lamp_connected\":" + jsonBool(RoomLight::isConnected()) + "}";
  } else {
    json += ",\"sunrise\":null";
  }

  // Also when they are not among the next two alarms
  AlarmClock::Alarm alarm;
  json += ",\"one_time\":" +
          (AlarmClock::oneTime(alarm) ? alarmJson(alarm, now) : String("null"));
  json += ",\"snooze\":" +
          (AlarmClock::snoozed(alarm) ? alarmJson(alarm, now) : String("null"));

  // For the snooze buttons after an alarm
  if (AlarmClock::justRang(alarm)) {
    json += ",\"rang\":{\"time\":\"" + timeText(alarm.hour, alarm.minute) + "\"" +
            ",\"minutes_ago\":" + String((long)(now - alarm.at) / 60) + "}";
  } else {
    json += ",\"rang\":null";
  }

  return json;
}

void sendStatus(WebServer& server) {
  sendOk(server, statusFields());
}

// Sends the status, or the error if there is one.
void sendResult(WebServer& server, const char* error) {
  if (error) {
    sendError(server, 400, error);
    return;
  }
  sendStatus(server);
}

void handleSave(WebServer& server) {
  int id = -1;
  if (!readIntParam(server, "id", id, 0, AlarmClock::MAX_SCHEDULES - 1)) return;
  if (id >= AlarmClock::scheduleCount()) {
    sendError(server, 404, "Unknown schedule");
    return;
  }

  // Start from the saved schedule, so a change can send only some fields.
  int days = -1, hour = -1, minute = -1, enabled = 1;
  int fade = AlarmClock::DEFAULT_FADE_MINUTES;
  int brightness = AlarmClock::DEFAULT_BRIGHTNESS;
  int temperature = AlarmClock::DEFAULT_TEMPERATURE;

  if (id >= 0) {
    AlarmClock::Schedule s = AlarmClock::schedule(id);
    days = s.days;
    hour = s.hour;
    minute = s.minute;
    enabled = s.enabled;
    fade = s.fadeMinutes;
    brightness = s.brightness;
    temperature = s.temperature;
  }

  if (!readIntParam(server, "days", days, 1, 127) ||
      !readIntParam(server, "hour", hour, 0, 23) ||
      !readIntParam(server, "minute", minute, 0, 59) ||
      !readIntParam(server, "enabled", enabled, 0, 1) ||
      !readIntParam(server, "fade", fade, 0, 60) ||
      !readIntParam(server, "brightness", brightness, 1, 100) ||
      !readIntParam(server, "temperature", temperature, 0, 100)) {
    return;
  }

  if (days < 0 || hour < 0 || minute < 0) {
    sendError(server, 400, "Provide 'days', 'hour' and 'minute'");
    return;
  }

  AlarmClock::Schedule schedule = {
      (uint8_t)days,       (uint8_t)hour,       (uint8_t)minute,
      (uint8_t)fade,       (uint8_t)brightness, (uint8_t)temperature,
      enabled == 1};
  sendResult(server, AlarmClock::saveSchedule(id, schedule));
}

void handleDelete(WebServer& server) {
  int id = -1;
  if (!readIntParam(server, "id", id, 0, AlarmClock::MAX_SCHEDULES - 1)) return;
  if (id < 0) {
    sendError(server, 400, "Provide 'id'");
    return;
  }
  sendResult(server, AlarmClock::deleteSchedule(id));
}

void handleNext(WebServer& server) {
  int date = 0, hour = -1, minute = -1, skip = 0;

  if (!readIntParam(server, "date", date, 0, 99991231) ||
      !readIntParam(server, "hour", hour, 0, 23) ||
      !readIntParam(server, "minute", minute, 0, 59) ||
      !readIntParam(server, "skip", skip, 0, 1)) {
    return;
  }

  if (skip == 1) {
    sendResult(server, AlarmClock::skipNext(date));
  } else if (hour < 0 || minute < 0) {
    sendError(server, 400, "Provide 'hour' and 'minute', or 'skip'");
  } else {
    sendResult(server, AlarmClock::changeNext(date, hour, minute));
  }
}

void handleOnce(WebServer& server) {
  int hour = -1, minute = -1;
  if (!readIntParam(server, "hour", hour, 0, 23) ||
      !readIntParam(server, "minute", minute, 0, 59)) {
    return;
  }
  if (hour < 0 || minute < 0) {
    sendError(server, 400, "Provide 'hour' and 'minute'");
    return;
  }
  sendResult(server, AlarmClock::setOneTime(hour, minute));
}

void handleSnooze(WebServer& server) {
  int minutes = 10;
  if (!readIntParam(server, "minutes", minutes, 1, 120)) return;

  const char* error = AlarmClock::snooze(minutes);
  if (error) {
    sendError(server, 400, error);
    return;
  }

  // Dark until then. Connects first if needed, which can take a few seconds.
  bool lightOff = RoomLight::ensureConnected() && RoomLight::powerOff();
  sendOk(server, statusFields() + ",\"light_off\":" + jsonBool(lightOff));
}

}  // namespace

void registerAlarmRoutes(WebServer& server) {
  server.on("/alarm/status", HTTP_ANY, [&server]() { sendStatus(server); });
  server.on("/alarm/on", HTTP_ANY, [&server]() {
    AlarmClock::setEnabled(true);
    sendStatus(server);
  });
  server.on("/alarm/off", HTTP_ANY, [&server]() {
    AlarmClock::setEnabled(false);
    sendStatus(server);
  });
  server.on("/alarm/save", HTTP_ANY, [&server]() { handleSave(server); });
  server.on("/alarm/delete", HTTP_ANY, [&server]() { handleDelete(server); });
  server.on("/alarm/next", HTTP_ANY, [&server]() { handleNext(server); });
  server.on("/alarm/next/reset", HTTP_ANY, [&server]() {
    AlarmClock::resetNext();
    sendStatus(server);
  });
  server.on("/alarm/once", HTTP_ANY, [&server]() { handleOnce(server); });
  server.on("/alarm/once/remove", HTTP_ANY, [&server]() {
    AlarmClock::removeOneTime();
    sendStatus(server);
  });
  server.on("/alarm/snooze", HTTP_ANY, [&server]() { handleSnooze(server); });
  server.on("/alarm/snooze/cancel", HTTP_ANY, [&server]() {
    AlarmClock::cancelSnooze();
    sendStatus(server);
  });
  server.on("/alarm/stop", HTTP_ANY, [&server]() {
    AlarmClock::stopSunrise();
    sendStatus(server);
  });
  server.on("/alarm", HTTP_GET, [&server]() { sendAlarmPage(server); });
}
