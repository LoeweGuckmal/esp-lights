#include "presence_http.h"

#include "config.h"
#include "http_utils.h"
#include "presence.h"
#include "presence_page.h"

namespace {

const char* reasonText(Presence::Reason reason) {
  switch (reason) {
    case Presence::Reason::Alarm: return "alarm";
    case Presence::Reason::Watch: return "watch";
    default: return "manual";
  }
}

const char* answerText(Presence::Answer answer) {
  switch (answer) {
    case Presence::Answer::Checking: return "checking";
    case Presence::Answer::Present: return "present";
    case Presence::Answer::Away: return "away";
    default: return "unknown";
  }
}

// Everything the page needs in one answer. RSSI values are negative,
// so they don't go through jsonNumber().
String statusFields() {
  String json = "\"enabled\":" + jsonBool(Presence::enabled());
  json += ",\"phone\":" + jsonBool(Presence::hasPhone());
  json += ",\"answer\":\"" + String(answerText(Presence::answer())) + "\"";

  uint32_t pairing = Presence::pairingLeftS();
  json += ",\"pairing\":" + (pairing ? String(pairing) : String("null"));
  json += ",\"pair_ok\":" + jsonBool(Presence::lastPairingWorked());

  Presence::Result r = Presence::result();
  if (r.valid) {
    json += ",\"check\":{\"running\":" + jsonBool(r.running) +
            ",\"reason\":\"" + reasonText(r.reason) + "\"" +
            ",\"seen\":" + jsonBool(r.seen) +
            ",\"found\":" + jsonBool(r.found) +
            ",\"rssi\":" + (r.seen ? String(r.rssi) : String("null")) +
            ",\"best_rssi\":" + (r.seen ? String(r.bestRssi) : String("null")) +
            ",\"heard_ago_s\":" + (r.seen ? String(r.heardAgoS) : String("null")) +
            ",\"ago_s\":" + String(r.ageS) +
            ",\"left_s\":" + String(r.leftS) + "}";
  } else {
    json += ",\"check\":null";
  }

  // Settings from config.h, for the texts on the page
  json += ",\"device_name\":\"" + String(BLE_DEVICE_NAME) + "\"";
  json += ",\"min_rssi\":" + String(PRESENCE_MIN_RSSI);
  json += ",\"scan_s\":" + String(PRESENCE_SCAN_S);
  json += ",\"lead_s\":" + String(PRESENCE_CHECK_LEAD_S);
  return json;
}

void sendStatus(WebServer& server) {
  sendOk(server, statusFields());
}

// Sends the status, or the error if there is one.
void sendResult(WebServer& server, const char* error) {
  if (error) {
    sendError(server, 400, error);
    return;
  }
  sendStatus(server);
}

void handleCheck(WebServer& server) {
  int watch = 0;
  if (!readIntParam(server, "watch", watch, 0, 1)) return;
  sendResult(server, Presence::check(watch ? Presence::Reason::Watch
                                           : Presence::Reason::Manual));
}

}  // namespace

void registerPresenceRoutes(WebServer& server) {
  server.on("/presence/status", HTTP_ANY, [&server]() { sendStatus(server); });
  server.on("/presence/on", HTTP_ANY, [&server]() {
    Presence::setEnabled(true);
    sendStatus(server);
  });
  server.on("/presence/off", HTTP_ANY, [&server]() {
    Presence::setEnabled(false);
    sendStatus(server);
  });
  server.on("/presence/check", HTTP_ANY, [&server]() { handleCheck(server); });
  server.on("/presence/stop", HTTP_ANY, [&server]() {
    Presence::stopCheck();
    sendStatus(server);
  });
  server.on("/presence/pair", HTTP_ANY, [&server]() {
    sendResult(server, Presence::startPairing());
  });
  server.on("/presence/pair/cancel", HTTP_ANY, [&server]() {
    Presence::stopPairing();
    sendStatus(server);
  });
  server.on("/presence/forget", HTTP_ANY, [&server]() {
    Presence::forgetPhone();
    sendStatus(server);
  });
  server.on("/presence", HTTP_GET, [&server]() { sendPresencePage(server); });
}
