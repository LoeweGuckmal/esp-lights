#pragma once

#include <WebServer.h>

// HTTP routes for the daylight switching, same style as the alarms: GET or
// POST, answers in JSON. There is no page; open them in the browser.
//   /daylight/status    forecast, what the sun and the weather say, the next
//                       changes and a running switch
//   /daylight/log       every switch, newest first: when, why, the result
//                       and the brightness
//   /daylight/on        master switch
//   /daylight/off
//   /daylight/refresh   downloads the forecast now
void registerDaylightRoutes(WebServer& server);
