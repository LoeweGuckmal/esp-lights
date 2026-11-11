// Wi-Fi test for the board of the lights controller: only Wi-Fi, no
// Bluetooth, no lights. It shows whether the board's Wi-Fi itself works, so
// bad Wi-Fi can be pinned on the board, the router or the lights firmware.
// How to read the output: README.md, "Wi-Fi test".
//
// It uses the Wi-Fi name and password the lights firmware saved on the
// board. For a board that never ran it, copy lights/secrets.h into this
// folder. Nothing the lights firmware saved (alarms, phone, log) is touched.

#include <WebServer.h>
#include <WiFi.h>
#include <esp_phy_init.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include "ping/ping_sock.h"

#include "page.h"

#if __has_include("secrets.h")
#include "secrets.h"
const char* const SECRET_SSID = WIFI_SSID;
const char* const SECRET_PASSWORD = WIFI_PASSWORD;
#else
const char* const SECRET_SSID = nullptr;
const char* const SECRET_PASSWORD = nullptr;
#endif

constexpr uint8_t STATUS_LED_PIN = 48;         // onboard RGB LED, as in lights/config.h
constexpr uint32_t STATUS_EVERY_MS = 5000;     // one line in the serial monitor
constexpr uint32_t PING_EVERY_MS = 500;        // to the router
constexpr uint32_t PING_TIMEOUT_MS = 1000;
constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t RECONNECT_MS = 30000;       // like WIFI_RECONNECT_S of the lights
constexpr int SCAN_ROWS = 25;

// 't' steps through these, in quarters of a dBm.
const wifi_power_t TX_POWERS[] = {WIFI_POWER_20dBm, WIFI_POWER_15dBm, WIFI_POWER_11dBm,
                                  WIFI_POWER_8_5dBm};

WebServer server(80);
char ssid[33] = "";
char password[65] = "";

// Written from the network event task.
volatile bool online = false;
volatile uint32_t drops = 0;
volatile unsigned long lastDropMs = 0;

// Written from the ping task.
esp_ping_handle_t pingSession = nullptr;
volatile uint32_t pingDone = 0;   // answered or timed out
volatile uint32_t pingBack = 0;
volatile uint32_t pingSumMs = 0;
volatile uint32_t pingMaxMs = 0;  // since the last status line

uint32_t webRequests = 0;
int txStep = 0;
unsigned long lastStatusMs = 0;
unsigned long lastRetryMs = 0;
bool wasOffline = false;


const char* signalText(int rssi) {
  if (rssi >= -60) return "very good";
  if (rssi >= -67) return "good";
  if (rssi >= -75) return "okay";
  if (rssi >= -82) return "weak";
  return "very weak";
}

const char* startReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "restart by software";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog, it hung";
    case ESP_RST_BROWNOUT: return "brownout, the power supply dipped";
    case ESP_RST_USB: return "over USB, an upload say";
    default: return "other";
  }
}

// The driver counts in quarters of a dBm.
float txPowerDbm() {
  return (int)WiFi.getTxPower() / 4.0f;
}

void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  s = Scan: every network in reach, with its signal");
  Serial.println("  r = Connect again");
  Serial.println("  t = Sending power: 20 -> 15 -> 11 -> 8.5 dBm -> 20");
  Serial.println("  p = Power saving on/off (the lights firmware needs it on)");
  Serial.println("  c = Calibrate the radio from scratch (restarts)");
  Serial.println("  ? = Show commands");
  Serial.println();
}

// Runs in the network event task.
void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    if (online) return;
    online = true;
    if (drops > 0) {
      Serial.printf("Connected again after %lu s\n", (millis() - lastDropMs) / 1000);
    }
  } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    // While reconnecting, every failed attempt ends up here too.
    if (!online) return;
    online = false;
    drops = drops + 1;
    lastDropMs = millis();
    uint8_t reason = info.wifi_sta_disconnected.reason;
    Serial.printf("Connection lost, reason %u %s, signal %d dBm\n", reason,
                  WiFi.disconnectReasonName((wifi_err_reason_t)reason),
                  info.wifi_sta_disconnected.rssi);
  }
}

// Run in the ping task.
void onPingBack(esp_ping_handle_t session, void*) {
  uint32_t ms = 0;
  esp_ping_get_profile(session, ESP_PING_PROF_TIMEGAP, &ms, sizeof(ms));
  pingSumMs = pingSumMs + ms;
  if (ms > pingMaxMs) pingMaxMs = ms;
  pingBack = pingBack + 1;
  pingDone = pingDone + 1;
}

void onPingTimeout(esp_ping_handle_t, void*) {
  pingDone = pingDone + 1;
}

// Pings the router from now on, also across reconnects.
void startPing() {
  IPAddress gateway = WiFi.gatewayIP();
  ip_addr_t target;
  IP_ADDR4(&target, gateway[0], gateway[1], gateway[2], gateway[3]);

  esp_ping_config_t config = {};
  config.count = ESP_PING_COUNT_INFINITE;
  config.interval_ms = PING_EVERY_MS;
  config.timeout_ms = PING_TIMEOUT_MS;
  config.data_size = 32;
  config.ttl = 64;
  config.target_addr = target;
  config.task_stack_size = 4096;
  config.task_prio = 2;

  esp_ping_callbacks_t callbacks = {};
  callbacks.on_ping_success = onPingBack;
  callbacks.on_ping_timeout = onPingTimeout;

  if (esp_ping_new_session(&config, &callbacks, &pingSession) != ESP_OK ||
      esp_ping_start(pingSession) != ESP_OK) {
    pingSession = nullptr;
    Serial.println("Could not start pinging the router.");
  }
}

// The name and password to connect with. False if there are none.
bool findCredentials() {
  if (SECRET_SSID) {
    strlcpy(ssid, SECRET_SSID, sizeof(ssid));
    strlcpy(password, SECRET_PASSWORD, sizeof(password));
    Serial.printf("Wi-Fi \"%s\", from secrets.h\n", ssid);
    return true;
  }

  // What the lights firmware connected with last: the Wi-Fi driver keeps
  // it in flash. Neither is zero-terminated at full length; ssid and
  // password are a byte longer and start out as zeros.
  wifi_config_t saved = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &saved) != ESP_OK || saved.sta.ssid[0] == 0) return false;
  memcpy(ssid, saved.sta.ssid, sizeof(saved.sta.ssid));
  memcpy(password, saved.sta.password, sizeof(saved.sta.password));
  Serial.printf("Wi-Fi \"%s\", as saved by the lights firmware\n", ssid);
  return true;
}

void scan() {
  Serial.println("Scanning, about 5 s...");
  int count = WiFi.scanNetworks(false, true);
  if (count < 0) {
    Serial.printf("The scan failed (%d).\n", count);
    return;
  }

  String connectedTo = WiFi.isConnected() ? WiFi.BSSIDstr() : String();
  int own = 0;
  Serial.printf("%d network(s) in reach, strongest first:\n", count);
  Serial.println("   dBm  ch  access point       name");
  for (int i = 0; i < count; i++) {
    String name = WiFi.SSID(i);
    String accessPoint = WiFi.BSSIDstr(i);
    bool mine = name == ssid;
    if (mine) own++;
    if (i >= SCAN_ROWS && !mine) continue;
    Serial.printf("  %4d  %2d  %s  %s%s\n", (int)WiFi.RSSI(i), (int)WiFi.channel(i),
                  accessPoint.c_str(), name.length() ? name.c_str() : "(hidden)",
                  !mine ? "" : accessPoint == connectedTo ? "  <- connected" : "  <- yours");
  }
  WiFi.scanDelete();

  if (own == 0) {
    Serial.printf("\"%s\" is not among them.\n", ssid);
  } else if (own > 1) {
    Serial.printf("\"%s\" comes from %d access points. This test connects to the strongest.\n"
                  "Compare with the access point W prints in the lights firmware.\n", ssid, own);
  }
  Serial.println("Right beside the router, a healthy board reads about -30 to -50 dBm.");
}

void printConnection() {
  int rssi = WiFi.RSSI();
  Serial.printf("Connected: %d dBm (%s), access point %s, channel %d\n", rssi, signalText(rssi),
                WiFi.BSSIDstr().c_str(), (int)WiFi.channel());
  Serial.printf("IP %s, router %s, sending power %.1f dBm, power saving %s\n",
                WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(),
                txPowerDbm(), WiFi.getSleep() != WIFI_PS_NONE ? "on" : "off");
  Serial.printf("The page with the live values: http://%s/\n", WiFi.localIP().toString().c_str());
}

// Blocks until connected, at most CONNECT_TIMEOUT_MS.
void connectWifi() {
  // All channels, then the strongest access point with this name, like
  // the lights firmware. The default stops at the first one it finds.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(ssid, password);

  Serial.print("Connecting");
  unsigned long start = millis();
  while (!WiFi.isConnected() && millis() - start < CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.isConnected()) {
    Serial.printf("Took %.1f s.\n", (millis() - start) / 1000.0);
    printConnection();
  } else {
    Serial.printf("Not connected after %lu s. It keeps trying.\n",
                  (unsigned long)(CONNECT_TIMEOUT_MS / 1000));
  }
}

void nextTxPower() {
  txStep = (txStep + 1) % (sizeof(TX_POWERS) / sizeof(TX_POWERS[0]));
  WiFi.setTxPower(TX_POWERS[txStep]);
  Serial.printf("Sending power: %.1f dBm\n", txPowerDbm());
}

void togglePowerSaving() {
  WiFi.setSleep(WiFi.getSleep() == WIFI_PS_NONE);
  Serial.printf("Power saving: %s\n", WiFi.getSleep() != WIFI_PS_NONE ? "on" : "off");
}

// The radio keeps its calibration in flash and only refines it on a normal
// start. Without it, the next start measures everything from scratch.
void calibrate() {
  esp_err_t error = esp_phy_erase_cal_data_in_nvs();
  if (error != ESP_OK) {
    Serial.printf("Could not erase the calibration (error %d).\n", error);
    return;
  }
  Serial.println("Calibration erased. Restarting, the radio calibrates from scratch...");
  delay(500);
  ESP.restart();
}

void handleSerial() {
  if (!Serial.available()) return;

  switch (Serial.read()) {
    case 's': scan(); break;
    case 'r':
      WiFi.disconnect();
      delay(200);
      connectWifi();
      break;
    case 't': nextTxPower(); break;
    case 'p': togglePowerSaving(); break;
    case 'c': calibrate(); break;
    case '?': printHelp(); break;
    case '\r':
    case '\n':
    case ' ': break;
    default: Serial.println("Unknown command. Enter ? for help."); break;
  }
}

void sendData() {
  webRequests++;
  uint32_t done = pingDone;
  uint32_t back = pingBack;
  int rssi = WiFi.RSSI();

  String json = "{\"rssi\":" + String(rssi);
  json += ",\"rating\":\"" + String(signalText(rssi)) + "\"";
  json += ",\"ap\":\"" + WiFi.BSSIDstr() + "\"";
  json += ",\"channel\":" + String(WiFi.channel());
  json += ",\"drops\":" + String(drops);
  json += ",\"uptime\":" + String(millis() / 1000);
  json += ",\"tx\":" + String(txPowerDbm(), 1);
  json += ",\"saving\":" + String(WiFi.getSleep() != WIFI_PS_NONE ? "true" : "false");
  json += ",\"ping_done\":" + String(done);
  json += ",\"ping_back\":" + String(back);
  json += ",\"ping_avg\":" + String(back ? pingSumMs / back : 0);
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

// One line: the signal, and the pings and web requests since the last line.
void printStatus() {
  static uint32_t lastDone = 0, lastBack = 0, lastSumMs = 0, lastRequests = 0;

  if (!WiFi.isConnected()) {
    Serial.printf("%6lu s  not connected\n", millis() / 1000);
    return;
  }

  uint32_t done = pingDone, back = pingBack, sumMs = pingSumMs, maxMs = pingMaxMs;
  pingMaxMs = 0;
  uint32_t newDone = done - lastDone, newBack = back - lastBack;

  int rssi = WiFi.RSSI();
  Serial.printf("%6lu s  %d dBm (%s)  pings %lu of %lu back", millis() / 1000, rssi,
                signalText(rssi), (unsigned long)newBack, (unsigned long)newDone);
  if (newBack > 0) {
    Serial.printf(", avg %lu ms, max %lu ms", (unsigned long)((sumMs - lastSumMs) / newBack),
                  (unsigned long)maxMs);
  }
  Serial.printf("  web requests %lu\n", (unsigned long)(webRequests - lastRequests));

  lastDone = done;
  lastBack = back;
  lastSumMs = sumMs;
  lastRequests = webRequests;
}

void setup() {
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  // As in the lights firmware: never wait for a serial monitor that isn't
  // reading, drop the output instead.
  Serial.setTxBufferSize(2048);
  Serial.setTxTimeoutMs(0);
#endif
  Serial.begin(115200);
  delay(1000);
  rgbLedWrite(STATUS_LED_PIN, 0, 0, 0);  // the lights firmware may have left it on

  Serial.println("\nWi-Fi test (no Bluetooth)");
  Serial.printf("Started by: %s\n", startReason());
  if (esp_reset_reason() == ESP_RST_BROWNOUT) {
    Serial.println("After a brownout the board sends weaker on purpose. Try another cable or "
                   "power supply, then unplug it once.");
  }

  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_STA);

  if (!findCredentials()) {
    Serial.println("No Wi-Fi name saved on this board. Copy lights/secrets.h into the "
                   "wifi_test folder and upload again.");
    while (true) delay(1000);
  }

  scan();
  connectWifi();

  server.on("/", HTTP_GET, []() {
    webRequests++;
    server.send_P(200, "text/html", PAGE);
  });
  server.on("/data", HTTP_GET, sendData);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();

  printHelp();
}

void loop() {
  handleSerial();
  server.handleClient();

  bool connected = WiFi.isConnected();
  if (connected && !pingSession) startPing();

  // Like the lights firmware: the ESP32 core reconnects by itself first,
  // then every RECONNECT_MS from here.
  unsigned long now = millis();
  if (connected) {
    wasOffline = false;
  } else if (!wasOffline) {
    wasOffline = true;
    lastRetryMs = now;
  } else if (now - lastRetryMs >= RECONNECT_MS) {
    lastRetryMs = now;
    Serial.println("Still offline, connecting again...");
    WiFi.reconnect();
  }

  if (now - lastStatusMs >= STATUS_EVERY_MS) {
    lastStatusMs = now;
    printStatus();
  }

  delay(2);
}
