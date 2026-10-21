#pragma once

#include <Arduino.h>
#include <time.h>

// The forecast from open-meteo.com (free, no API key) for the location in
// secrets.h: the solar radiation every 15 minutes for the next 24 hours,
// and sunrise and sunset of today and tomorrow.
namespace Weather {

constexpr time_t SLOT_S = 15 * 60;  // one radiation value every 15 minutes
constexpr int SLOTS = 97;           // the one before now, and 24 hours ahead
constexpr int DAYS = 2;             // today and tomorrow (UTC days)

struct Forecast {
  time_t start;            // time of radiation[0]
  int count;               // values in radiation[], 0 = no forecast
  float radiation[SLOTS];  // shortwave radiation in W/m²
  int days;                // values in sunrise[] and sunset[]
  time_t sunrise[DAYS];
  time_t sunset[DAYS];
};

// Downloads the forecast, about 2 KB. Plain HTTP: TLS would need about
// 40 KB of memory for a few public numbers. Blocks, usually for well under
// a second, at most for the timeouts in weather.cpp.
// Returns nullptr on success, otherwise an error message; `out` is only
// changed on success.
const char* fetch(Forecast& out);

// The radiation at `t`, on a straight line between the values around it.
// NAN outside the forecast.
float radiationAt(const Forecast& forecast, time_t t);

}  // namespace Weather
