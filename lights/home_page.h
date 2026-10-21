#pragma once

#include <WebServer.h>

// The start page at /: on and off for both lights, and links to their
// pages and the alarms. It uses the routes in led_http.h and
// room_light_http.h.
void registerHomeRoute(WebServer& server);
