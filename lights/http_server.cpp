#include "http_server.h"

#include "config.h"

static WebServer server(HTTP_PORT);

WebServer& httpServer() {
  return server;
}

void startHttpServer() {
  server.onNotFound([]() {
    server.send(404, "application/json",
                "{\"error\":\"Not found\"}");
  });

  server.begin();
  Serial.println("HTTP server started");
}

void handleHttpClients() {
  server.handleClient();
}
