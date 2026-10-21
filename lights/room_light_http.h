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
//        /room/status
void registerRoomLightRoutes(WebServer& server);
