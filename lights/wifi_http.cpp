#include "wifi_http.h"

#include <WiFi.h>
#include <esp_timer.h>

#include "http_utils.h"
#include "wifi_connection.h"

namespace {

// RSSI values are negative, so they don't go through jsonNumber().
String statusFields() {
  WifiStats s = wifiStats();

  String json;
  json.reserve(640);
  json = "\"connected\":" + jsonBool(s.connected);
  if (s.connected) {
    int rssi = WiFi.RSSI();
    json += ",\"rssi\":" + String(rssi) + ",\"rating\":\"" + wifiSignalText(rssi) + "\"";
    json += ",\"access_point\":\"" + WiFi.BSSIDstr() + "\"";
    json += ",\"channel\":" + String(WiFi.channel());
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  }
  json += ",\"pinned\":" + jsonBool(s.pinned);
  json += String(s.connected ? ",\"connected_s\":" : ",\"offline_s\":") + String(s.sinceS);

  if (s.weakestRssi < 0) {
    json += ",\"weakest\":{\"rssi\":" + String(s.weakestRssi) +
            ",\"ago_s\":" + String(s.weakestRssiAgoS) + "}";
  } else {
    json += ",\"weakest\":null";
  }

  json += ",\"drops\":" + String(s.drops);
  if (s.drops > 0) {
    json += ",\"last_drop\":{\"reason\":" + String(s.lastDropReason) + ",\"name\":\"" +
            WiFi.disconnectReasonName((wifi_err_reason_t)s.lastDropReason) +
            "\",\"rssi\":" + String(s.lastDropRssi) +
            ",\"ago_s\":" + String(s.lastDropAgoS) + "}";
  } else {
    json += ",\"last_drop\":null";
  }

  if (s.lastSwitch == WifiSwitch::None) {
    json += ",\"switch\":null";
  } else {
    json += ",\"switch\":{\"state\":\"" + String(wifiSwitchName(s.lastSwitch)) +
            "\",\"ago_s\":" + String(s.lastSwitchAgoS) + "}";
  }

  json += ",\"uptime_s\":" + String((uint32_t)(esp_timer_get_time() / 1000000));
  json += ",\"memory\":{\"free\":" + String(ESP.getFreeHeap()) +
          ",\"lowest\":" + String(ESP.getMinFreeHeap()) +
          ",\"largest_block\":" + String(ESP.getMaxAllocHeap()) + "}";
  return json;
}

}  // namespace

void registerWifiRoutes(WebServer& server) {
  server.on("/wifi/status", HTTP_ANY, [&server]() { sendOk(server, statusFields()); });
  server.on("/wifi/switch", HTTP_ANY, [&server]() {
    if (!requestWifiSwitch()) {
      sendError(server, 409, "A switch is already running");
      return;
    }
    sendOk(server, statusFields());
  });
}
