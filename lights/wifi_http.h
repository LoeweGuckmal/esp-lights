#pragma once

#include <WebServer.h>

// HTTP routes for the Wi-Fi connection itself, GET or POST:
//   /wifi/status   signal, access point, lost connections, memory: what the
//                  serial command W prints
//   /wifi/switch   moves to the other access point of the network, for when
//                  this one is slow. Answers first and switches then, which
//                  takes a few seconds; /wifi/status has the result.
void registerWifiRoutes(WebServer& server);
