#pragma once

#include <WebServer.h>

// The one HTTP server shared by all lights.
// Each light registers its routes on httpServer() before startHttpServer().
WebServer& httpServer();

void startHttpServer();

// Call from loop().
void handleHttpClients();
