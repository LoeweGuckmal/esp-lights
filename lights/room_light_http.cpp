#include "room_light_http.h"

#include "alarm_clock.h"
#include "http_utils.h"
#include "light_page.h"
#include "room_light.h"

namespace {

void sendNotConnected(WebServer& server) {
  sendError(server, 503, "Room light not connected");
}

// Connects first if needed, which can take a few seconds.
bool ensureLampConnected(WebServer& server) {
  if (RoomLight::ensureConnected()) return true;
  sendNotConnected(server);
  return false;
}

// 0..100 from the API to the lamp's 0..127
int toLampScale(int percent) {
  return (percent * 127 + 50) / 100;
}

void handleRgb(WebServer& server) {
  int r = -1, g = -1, b = -1, brightness = -1;

  if (!readIntParam(server, "r", r, 0, 255) ||
      !readIntParam(server, "g", g, 0, 255) ||
      !readIntParam(server, "b", b, 0, 255) ||
      !readIntParam(server, "brightness", brightness, 1, 100)) {
    return;
  }

  bool hasColor = r >= 0 || g >= 0 || b >= 0;
  if (hasColor && (r < 0 || g < 0 || b < 0)) {
    sendError(server, 400, "Provide all of 'r', 'g' and 'b'");
    return;
  }
  if (!hasColor && brightness < 0) {
    sendError(server, 400, "Provide 'r', 'g', 'b' and/or 'brightness'");
    return;
  }

  // A manual setting takes over from a running sunrise.
  AlarmClock::stopSunrise();
  if (!ensureLampConnected(server)) return;

  bool sent = true;
  // The lamp accepts 10..100 for color brightness.
  if (brightness >= 0) sent = RoomLight::setColorBrightness(max(10, brightness));
  if (sent && hasColor) sent = RoomLight::setColor(r, g, b);

  if (!sent) {
    sendNotConnected(server);
    return;
  }

  sendOk(server, "\"mode\":\"color\",\"r\":" + jsonNumber(r) +
                     ",\"g\":" + jsonNumber(g) +
                     ",\"b\":" + jsonNumber(b) +
                     ",\"brightness\":" + jsonNumber(brightness));
}

void handleWhite(WebServer& server) {
  int temperature = -1, brightness = -1;

  if (!readIntParam(server, "temperature", temperature, 0, 100) ||
      !readIntParam(server, "brightness", brightness, 1, 100)) {
    return;
  }

  if (temperature < 0 && brightness < 0) {
    temperature = 50;
    brightness = 70;
  }

  AlarmClock::stopSunrise();
  if (!ensureLampConnected(server)) return;

  bool sent = true;
  if (temperature >= 0) sent = RoomLight::setWhiteTemperature(toLampScale(temperature));
  if (sent && brightness >= 0) {
    sent = RoomLight::setWhiteBrightness(max(1, toLampScale(brightness)));
  }

  if (!sent) {
    sendNotConnected(server);
    return;
  }

  sendOk(server, "\"mode\":\"white\",\"temperature\":" + jsonNumber(temperature) +
                     ",\"brightness\":" + jsonNumber(brightness));
}

void handleOn(WebServer& server) {
  if (!ensureLampConnected(server)) return;
  if (!RoomLight::powerOn()) {
    sendNotConnected(server);
    return;
  }
  sendOk(server, "\"power\":\"on\"");
}

void handleOff(WebServer& server) {
  AlarmClock::stopSunrise();
  if (!ensureLampConnected(server)) return;
  if (!RoomLight::powerOff()) {
    sendNotConnected(server);
    return;
  }
  sendOk(server, "\"power\":\"off\"");
}

// Same fields as the bridge, with the lamp's raw values.
void handleStatus(WebServer& server) {
  // Don't wait for a connection here, so the page loads fast.
  if (!RoomLight::isConnected()) RoomLight::connectNow();

  RoomLight::State s = RoomLight::readState();

  String state = "{";
  state += "\"status\":" + jsonNumber(s.status);
  state += ",\"is_on\":" + jsonBool(s.status == 1);
  state += ",\"mode\":" + jsonNumber(s.mode);
  state += ",\"white_brightness\":" + jsonNumber(s.whiteBrightness);
  state += ",\"white_temp\":" + jsonNumber(s.whiteTemp);
  state += ",\"color_brightness\":" + jsonNumber(s.colorBrightness);
  state += ",\"red\":" + jsonNumber(s.r);
  state += ",\"green\":" + jsonNumber(s.g);
  state += ",\"blue\":" + jsonNumber(s.b);
  state += "}";

  sendOk(server, "\"connected\":" + jsonBool(RoomLight::isConnected()) +
                     ",\"state\":" + state);
}

}  // namespace

void registerRoomLightRoutes(WebServer& server) {
  server.on("/room/rgb", HTTP_ANY, [&server]() { handleRgb(server); });
  server.on("/room/white", HTTP_ANY, [&server]() { handleWhite(server); });
  server.on("/room/on", HTTP_ANY, [&server]() { handleOn(server); });
  server.on("/room/off", HTTP_ANY, [&server]() { handleOff(server); });
  server.on("/room/status", HTTP_ANY, [&server]() { handleStatus(server); });
  server.on("/room", HTTP_GET, [&server]() {
    sendLightPage(server, "Room Light", "/room", true);
  });
}
