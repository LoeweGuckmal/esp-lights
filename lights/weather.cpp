#include "weather.h"

#include <WiFi.h>
#include <math.h>

#include "secrets.h"

namespace Weather {
namespace {

constexpr char HOST[] = "api.open-meteo.com";
constexpr int32_t CONNECT_TIMEOUT_MS = 5000;
// The whole answer must be in by then.
constexpr uint32_t READ_TIMEOUT_MS = 5000;

char errorText[48];

// The body is one JSON object: it's complete once its braces are closed.
bool bodyComplete(const String& response) {
  int start = response.indexOf("\r\n\r\n");
  if (start < 0) return false;

  int depth = 0;
  for (unsigned int i = start + 4; i < response.length(); i++) {
    if (response[i] == '{') depth++;
    if (response[i] == '}' && --depth == 0) return true;
  }
  return false;
}

// Sends the request and reads the answer, headers included.
const char* download(String& response) {
  if (WiFi.status() != WL_CONNECTED) return "Wi-Fi not connected";

  NetworkClient client;
  if (!client.connect(HOST, 80, CONNECT_TIMEOUT_MS)) {
    return "Could not connect to api.open-meteo.com";
  }

  // HTTP/1.0, so the body comes in one piece instead of in chunks.
  client.printf("GET /v1/forecast?latitude=%s&longitude=%s"
                "&minutely_15=shortwave_radiation_instant"
                "&past_minutely_15=1&forecast_minutely_15=%d"
                "&daily=sunrise,sunset&forecast_days=%d&timeformat=unixtime HTTP/1.0\r\n"
                "Host: %s\r\nConnection: close\r\n\r\n",
                WEATHER_LATITUDE, WEATHER_LONGITUDE, SLOTS - 1, DAYS, HOST);

  // The client doesn't reliably notice when the server closes the
  // connection, so stop as soon as the body is complete.
  response.reserve(2560);
  uint8_t buffer[256];
  unsigned long start = millis();
  while (!bodyComplete(response)) {
    if (millis() - start > READ_TIMEOUT_MS) {
      client.stop();
      return "api.open-meteo.com didn't answer in time";
    }
    int length = client.read(buffer, sizeof(buffer));
    if (length > 0) {
      response.concat((const char*)buffer, length);
    } else if (!client.connected()) {
      break;
    } else {
      delay(5);
    }
  }
  client.stop();
  return nullptr;
}

// The numbers in the JSON array that follows `key`, at most `max`.
// null becomes NAN. Returns how many were read.
int readNumbers(const char* json, const char* key, double* out, int max) {
  const char* p = strstr(json, key);
  if (!p) return 0;
  p += strlen(key);

  int count = 0;
  while (count < max && *p != ']') {
    if (strncmp(p, "null", 4) == 0) {
      out[count++] = NAN;
      p += 4;
    } else {
      char* end;
      double value = strtod(p, &end);
      if (end == p) break;  // not a number: the answer is cut off
      out[count++] = value;
      p = end;
    }
    if (*p == ',') p++;
  }
  return count;
}

}  // namespace

const char* fetch(Forecast& out) {
  String response;
  const char* error = download(response);
  if (error) return error;

  // "HTTP/1.1 200 OK", headers, empty line, body
  int code = response.startsWith("HTTP/") ? response.substring(9, 12).toInt() : 0;
  int bodyStart = response.indexOf("\r\n\r\n");
  if (code == 0 || bodyStart < 0) return "No valid answer from api.open-meteo.com";
  const char* json = response.c_str() + bodyStart + 4;

  if (code != 200) {
    // The body says why: {"error":true,"reason":"..."}
    Serial.printf("Weather: HTTP %d %s\n", code, json);
    snprintf(errorText, sizeof(errorText), "api.open-meteo.com answered HTTP %d", code);
    return errorText;
  }

  const char* minutely = strstr(json, "\"minutely_15\":{");
  const char* daily = strstr(json, "\"daily\":{");
  if (!minutely || !daily) return "Unexpected answer from api.open-meteo.com";

  double start;
  double values[SLOTS];
  double sunrise[DAYS];
  double sunset[DAYS];
  int count = readNumbers(minutely, "\"shortwave_radiation_instant\":[", values, SLOTS);
  int days = min(readNumbers(daily, "\"sunrise\":[", sunrise, DAYS),
                 readNumbers(daily, "\"sunset\":[", sunset, DAYS));
  for (int d = 0; d < days; d++) {
    if (isnan(sunrise[d]) || isnan(sunset[d])) days = d;
  }
  if (readNumbers(minutely, "\"time\":[", &start, 1) != 1 || isnan(start) ||
      count < 2 || days < 1) {
    return "Unexpected answer from api.open-meteo.com";
  }

  // A missing value (null) takes the one before, or the first known one.
  double last = NAN;
  for (int i = 0; i < count && isnan(last); i++) last = values[i];
  if (isnan(last)) return "No radiation in the forecast";

  out.start = (time_t)start;
  out.count = count;
  for (int i = 0; i < count; i++) {
    if (!isnan(values[i])) last = values[i];
    out.radiation[i] = last;
  }
  out.days = days;
  for (int d = 0; d < days; d++) {
    out.sunrise[d] = (time_t)sunrise[d];
    out.sunset[d] = (time_t)sunset[d];
  }
  return nullptr;
}

float radiationAt(const Forecast& forecast, time_t t) {
  if (forecast.count < 2 || t < forecast.start) return NAN;

  time_t offset = t - forecast.start;
  int i = offset / SLOT_S;
  if (i >= forecast.count - 1) return NAN;

  float part = (float)(offset % SLOT_S) / SLOT_S;
  return forecast.radiation[i] + (forecast.radiation[i + 1] - forecast.radiation[i]) * part;
}

}  // namespace Weather
