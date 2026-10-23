#pragma once

#include <WebServer.h>

// HTTP routes for the LED strip. Same design as the room light (see README):
// GET or POST, parameters in the query string or as a JSON body.
//   GET  /led          control page
//        /led/rgb      r, g, b (0-255), optional brightness (1-100)
//        /led/white    optional brightness (1-100, default 70)
//        /led/on
//        /led/off
//        /led/toggle   off if on, otherwise (or if unsure) on
//        /led/status
// rgb, white, on and toggle (when switching on) take if_dark=1: only while
// it's dark, see checkIfDark() in daylight_http.h.
void registerLedRoutes(WebServer& server);
