#pragma once

#include <WebServer.h>

// HTTP routes for the LED lamp:
//   GET  /led          control page
//   POST /led/color    {"r":0-255,"g":0-255,"b":0-255} or {"white":0-255}
//   GET  /led/on
//   GET  /led/off
//   GET  /led/status
void registerLedRoutes(WebServer& server);
