#pragma once

#include <WebServer.h>

// Sends the control page shared by all lights. The page talks to
// <basePath>/on, /off, /rgb, /white and /status.
// hasTemperature shows the white temperature slider.
void sendLightPage(WebServer& server, const char* title, const char* basePath,
                   bool hasTemperature);
