#pragma once

// Non-secret settings. Wi-Fi credentials live in secrets.h (ignored by git).

#include <stdint.h>

// --- Board ---
constexpr uint8_t STATUS_LED_PIN = 48;  // onboard RGB LED of the ESP32-S3 DevKit

// --- Bluetooth ---
constexpr char BLE_DEVICE_NAME[] = "ESP32-Lamp-Test";

// --- HTTP ---
constexpr uint16_t HTTP_PORT = 80;

// --- LED lamp (BLE) ---
// Address from your working Linux Python script
constexpr char LED_LAMP_ADDRESS[] = "C2:14:20:00:C4:0E";
constexpr uint8_t LED_LAMP_ADDRESS_TYPE = 0;  // 0 = public address
// How long one connection attempt may take, and how often to retry
// while the lamp is unreachable (both in milliseconds).
constexpr uint32_t LED_LAMP_CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t LED_LAMP_RETRY_INTERVAL_MS = 15000;

// --- Room light (AwoX mesh bulb, BLE) ---
// Mesh name and password are in secrets.h.
constexpr char ROOM_LIGHT_ADDRESS[] = "A4:C1:38:43:06:4D";
constexpr uint32_t ROOM_LIGHT_CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t ROOM_LIGHT_RETRY_INTERVAL_MS = 15000;
