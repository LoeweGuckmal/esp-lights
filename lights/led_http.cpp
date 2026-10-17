#include "led_http.h"

#include "http_utils.h"
#include "led_lamp.h"
#include "light_page.h"

namespace {

void sendNotConnected(WebServer& server) {
  sendError(server, 503, "LED lamp not connected");
}

// Connects first if needed, which can take a few seconds.
bool ensureLampConnected(WebServer& server) {
  if (LedLamp::ensureConnected()) return true;
  sendNotConnected(server);
  return false;
}

void handleRgb(WebServer& server) {
  int r = -1, g = -1, b = -1, brightness = -1;

  if (!readIntParam(server, "r", r, 0, 255) ||
      !readIntParam(server, "g", g, 0, 255) ||
      !readIntParam(server, "b", b, 0, 255) ||
      !readIntParam(server, "brightness", brightness, 1, 100)) {
    return;
  }

  if (r < 0 || g < 0 || b < 0) {
    sendError(server, 400, "Provide 'r', 'g' and 'b'");
    return;
  }

  if (!ensureLampConnected(server)) return;

  // The lamp has no brightness setting, so scale the color instead.
  int scale = brightness < 0 ? 100 : brightness;

  if (!LedLamp::setColor(r * scale / 100, g * scale / 100, b * scale / 100)) {
    sendNotConnected(server);
    return;
  }

  sendOk(server, "\"mode\":\"color\",\"r\":" + String(r) +
                     ",\"g\":" + String(g) +
                     ",\"b\":" + String(b) +
                     ",\"brightness\":" + jsonNumber(brightness));
}

void handleWhite(WebServer& server) {
  int brightness = 70;

  if (!readIntParam(server, "brightness", brightness, 1, 100)) return;
  if (!ensureLampConnected(server)) return;

  if (!LedLamp::setWhite((brightness * 255 + 50) / 100)) {
    sendNotConnected(server);
    return;
  }

  sendOk(server, "\"mode\":\"white\",\"brightness\":" + String(brightness));
}

void handleOn(WebServer& server) {
  if (!ensureLampConnected(server)) return;
  if (!LedLamp::powerOn()) {
    sendNotConnected(server);
    return;
  }
  sendOk(server, "\"power\":\"on\"");
}

void handleOff(WebServer& server) {
  if (!ensureLampConnected(server)) return;
  if (!LedLamp::powerOff()) {
    sendNotConnected(server);
    return;
  }
  sendOk(server, "\"power\":\"off\"");
}

void handleStatus(WebServer& server) {
  bool connected = LedLamp::isConnected();

  // Ask the lamp for an updated status notification. Don't wait for a
  // connection here, so the page loads fast.
  if (connected) {
    LedLamp::requestStatus();
  } else {
    LedLamp::connectNow();
  }

  // This is the latest notification received so far.
  LedLamp::Status status = LedLamp::lastStatus();

  String state = "{";
  state += "\"received\":" + jsonBool(status.received);
  state += ",\"is_on\":" + jsonBool(status.on);
  state += ",\"mode\":" + String(status.mode);
  state += ",\"red\":" + String(status.r);
  state += ",\"green\":" + String(status.g);
  state += ",\"blue\":" + String(status.b);
  state += ",\"white\":" + String(status.white);
  state += ",\"speed\":" + String(status.speed);
  state += ",\"age_ms\":" + String(status.ageMs);
  state += "}";

  sendOk(server, "\"connected\":" + jsonBool(connected) + ",\"state\":" + state);
}

}  // namespace

void registerLedRoutes(WebServer& server) {
  server.on("/led/rgb", HTTP_ANY, [&server]() { handleRgb(server); });
  server.on("/led/white", HTTP_ANY, [&server]() { handleWhite(server); });
  server.on("/led/on", HTTP_ANY, [&server]() { handleOn(server); });
  server.on("/led/off", HTTP_ANY, [&server]() { handleOff(server); });
  server.on("/led/status", HTTP_ANY, [&server]() { handleStatus(server); });
  server.on("/led", HTTP_GET, [&server]() {
    sendLightPage(server, "LED Lamp", "/led", false);
  });
}
