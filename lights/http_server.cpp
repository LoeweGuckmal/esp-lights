#include "http_server.h"

#include "config.h"
#include "http_utils.h"

static WebServer server(HTTP_PORT);

WebServer& httpServer() {
  return server;
}

void startHttpServer() {
  server.onNotFound([]() {
    sendError(server, 404, "Not found");
  });

  server.begin();
  Serial.println("HTTP server started");
}

void handleHttpClients() {
  server.handleClient();
}
