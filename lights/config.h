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
