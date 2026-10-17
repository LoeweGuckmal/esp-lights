#include "led_http.h"

#include "json_utils.h"
#include "led_lamp.h"
#include "led_page.h"

namespace {

void handleColor(WebServer& server) {
  String body = server.arg("plain");

  int r, g, b;

  if (!getJsonInt(body, "r", r) ||
      !getJsonInt(body, "g", g) ||
      !getJsonInt(body, "b", b)) {
    server.send(400, "application/json",
                "{\"error\":\"Expected RGB values from 0 to 255\"}");
    return;
  }

  LedLamp::setColor((uint8_t)r, (uint8_t)g, (uint8_t)b);

  String response = "{\"sent\":true,\"r\":" + String(r) +
                    ",\"g\":" + String(g) +
                    ",\"b\":" + String(b) + "}";

  server.send(200, "application/json", response);
}

void handleOn(WebServer& server) {
  LedLamp::powerOn();

  server.send(200, "application/json",
              "{\"sent\":true,\"command\":\"on\"}");
}

void handleOff(WebServer& server) {
  LedLamp::powerOff();

  server.send(200, "application/json",
              "{\"sent\":true,\"command\":\"off\"}");
}

void handleStatus(WebServer& server) {
  // Ask the lamp for an updated status notification.
  LedLamp::requestStatus();

  // This is the latest notification received so far.
  LedLamp::Status status = LedLamp::lastStatus();

  String response = "{";
  response += "\"received\":" + String(status.received ? "true" : "false");
  response += ",\"on\":" + String(status.on ? "true" : "false");
  response += ",\"rgb\":{\"r\":" + String(status.r) +
              ",\"g\":" + String(status.g) +
              ",\"b\":" + String(status.b) + "}";
  response += ",\"white\":" + String(status.white);
  response += ",\"mode\":" + String(status.mode);
  response += ",\"speed\":" + String(status.speed);
  response += ",\"age_ms\":" + String(status.ageMs);
  response += "}";

  server.send(200, "application/json", response);
}

}  // namespace

void registerLedRoutes(WebServer& server) {
  server.on("/led/color", HTTP_POST, [&server]() { handleColor(server); });
  server.on("/led/on", HTTP_GET, [&server]() { handleOn(server); });
  server.on("/led/off", HTTP_GET, [&server]() { handleOff(server); });
  server.on("/led/status", HTTP_GET, [&server]() { handleStatus(server); });
  server.on("/led", HTTP_GET, [&server]() {
    server.send_P(200, "text/html", LED_PAGE);
  });
}
