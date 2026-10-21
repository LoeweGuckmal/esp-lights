#pragma once

// Non-secret settings. Wi-Fi credentials live in secrets.h (ignored by git).

#include <stdint.h>

// --- Board ---
constexpr uint8_t STATUS_LED_PIN = 48;  // onboard RGB LED of the ESP32-S3 DevKit

// --- Bluetooth ---
constexpr char BLE_DEVICE_NAME[] = "ESP32-Lamp-Test";
// While a lamp is unreachable, the wait between connection attempts doubles
// from BLE_RETRY_MIN_MS up to BLE_RETRY_MAX_MS. A request for the lamp
// tries again right away.
constexpr uint32_t BLE_RETRY_MIN_MS = 15000;
constexpr uint32_t BLE_RETRY_MAX_MS = 600000;  // 10 min

// --- HTTP ---
constexpr uint16_t HTTP_PORT = 80;

// --- Clock and alarms ---
// Time zone as a POSIX TZ string. This one is Central Europe (Switzerland,
// Germany, ...) with daylight saving time.
constexpr char TIME_ZONE[] = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char NTP_SERVER_2[] = "time.google.com";
// Start connecting to the room light this long before a sunrise begins,
// so a slow retry backoff can't make the alarm late.
constexpr uint32_t ALARM_PRECONNECT_S = 120;
// If the room light is unreachable at the alarm time, keep trying this long.
constexpr uint32_t ALARM_RETRY_S = 600;
// The alarm page offers a snooze for this long after an alarm.
constexpr uint32_t ALARM_SNOOZE_OFFER_S = 3600;

// --- Presence (your phone, over BLE) ---
// Before an alarm, the ESP listens for your phone. If it isn't heard, the
// alarm doesn't ring. Turned on and off on the /presence page.
// Start looking this long before the sunrise begins. The room light only
// gets connected (ALARM_PRECONNECT_S) once the check is done, so the two
// don't compete for the radio.
constexpr uint32_t PRESENCE_CHECK_LEAD_S = 120;
// One check listens at most this long and ends as soon as the phone is
// heard. A locked iPhone advertises less often, so don't go much lower.
constexpr uint32_t PRESENCE_SCAN_S = 30;
// "Watch signal" on the page listens this long and shows every reading.
constexpr uint32_t PRESENCE_WATCH_S = 60;
// Weaker signals than this (in dBm) count as "not here". -100 takes
// everything the ESP can hear, which can be the whole flat. To count only
// your room, watch the signal from your bed and from the next room, then
// pick a value in between, e.g. -75.
constexpr int PRESENCE_MIN_RSSI = -100;
// How long the ESP can be paired with after "Pair phone".
constexpr uint32_t PRESENCE_PAIR_WINDOW_S = 120;

// --- Daylight (room light by the sun and the weather) ---
// The room light comes on when it gets dark and goes off when it gets
// bright, from the open-meteo.com forecast for the location in secrets.h.
// It only comes on when your phone is here (see Presence above).
// By the sun: on this long before sunset, off at sunrise.
constexpr uint32_t DAYLIGHT_SUNSET_LEAD_S = 45 * 60;
// By the weather: on when the solar radiation drops below this (in W/m²),
// off when it rises above it again. That catches dusk and dark, cloudy
// hours; a clear sky gives several hundred W/m² by day.
constexpr float DAYLIGHT_RADIATION_THRESHOLD = 50;
// Switching on fades in like a short sunrise, from the lowest brightness
// and warmest white to these (percent, temperature 0 = cold .. 100 = warm).
constexpr uint32_t DAYLIGHT_FADE_S = 300;
constexpr uint8_t DAYLIGHT_BRIGHTNESS = 70;
constexpr uint8_t DAYLIGHT_TEMPERATURE = 50;
// A switch that can't happen on time (lamp unreachable, ESP just started)
// is still tried this long.
constexpr uint32_t DAYLIGHT_RETRY_S = 600;

// --- LED strip (BLE) ---
// Address from your working Linux Python script
constexpr char LED_LAMP_ADDRESS[] = "C2:14:20:00:C4:0E";
constexpr uint8_t LED_LAMP_ADDRESS_TYPE = 0;  // 0 = public address
// How long one connection attempt may take
constexpr uint32_t LED_LAMP_CONNECT_TIMEOUT_MS = 5000;

// --- Room light (AwoX mesh bulb, BLE) ---
// Mesh name and password are in secrets.h.
constexpr char ROOM_LIGHT_ADDRESS[] = "A4:C1:38:43:06:4D";
constexpr uint32_t ROOM_LIGHT_CONNECT_TIMEOUT_MS = 5000;
