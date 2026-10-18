#pragma once

#include <WebServer.h>

// Sends the alarm page. It talks to the routes in alarm_http.h.
void sendAlarmPage(WebServer& server);
