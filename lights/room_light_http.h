#pragma once

#include <WebServer.h>

// HTTP routes for the room light: GET or POST, parameters in the query
// string or as a JSON body.
//   GET  /room          control page
//        /room/rgb      r, g, b (0-255) and/or brightness (1-100)
//        /room/white    temperature (0 = cold..100 = warm) and/or
//                       brightness (1-100); neither = 50 / 70
//        /room/on
//        /room/off
//        /room/toggle   off if on, otherwise (or if unsure) on
//        /room/status
// rgb, white, on and toggle (when switching on) take if_dark=1: only while
// it's dark, see checkIfDark() in daylight_http.h.
void registerRoomLightRoutes(WebServer& server);
