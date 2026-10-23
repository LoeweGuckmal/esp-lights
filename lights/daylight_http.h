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

// For the routes that switch a light on. Reads if_dark (0/1): with 1, the
// light only switches while it is dark (Daylight::darkNow(); unknown counts
// as dark). Returns true to go on. Otherwise it has answered already and the
// handler should just return: 400 for a bad value, and while it is bright
// {"ok":true,"switched":false,"dark":false}.
bool checkIfDark(WebServer& server);
