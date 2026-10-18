// Entry point: starts every module and keeps them running.
// See README.md for what lives in which file.

#include <NimBLEDevice.h>

#include "alarm_clock.h"
#include "alarm_http.h"
#include "config.h"
#include "http_server.h"
#include "led_http.h"
#include "led_lamp.h"
#include "room_light.h"
#include "room_light_http.h"
#include "serial_console.h"
#include "wifi_connection.h"

void setup() {
  Serial.begin(115200);
  delay(1000);
  rgbLedWrite(STATUS_LED_PIN, 0, 0, 0);

  Serial.println("\nESP32-S3 lights controller");

  // Bluetooth stack, shared by all BLE lights
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);  // moderate transmit power

  // Both connect in the background from loop(); the rest keeps working
  // while a lamp is off or out of range.
  LedLamp::begin();
  RoomLight::begin();

  printSerialHelp();

  connectWifi();

  // Needs Wi-Fi for the time
  AlarmClock::begin();

  registerLedRoutes(httpServer());
  registerRoomLightRoutes(httpServer());
  registerAlarmRoutes(httpServer());
  startHttpServer();
}

void loop() {
  handleSerialCommands();
  handleHttpClients();
  LedLamp::loop();
  RoomLight::loop();
  AlarmClock::loop();

  delay(10);
}
