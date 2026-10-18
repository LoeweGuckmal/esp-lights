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
