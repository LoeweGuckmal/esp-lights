#pragma once

#include <WebServer.h>

// Sends the presence page. It talks to the routes in presence_http.h.
void sendPresencePage(WebServer& server);
