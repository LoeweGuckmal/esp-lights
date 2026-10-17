#include <NimBLEDevice.h>
#include <WiFi.h>
#include <WebServer.h>

const char* WIFI_SSID = "SSID";
const char* WIFI_PASSWORD = "PASSWORD";

WebServer server(80);

// Latest status received from the lamp
volatile bool lampOn = false;
volatile int lampR = 0;
volatile int lampG = 0;
volatile int lampB = 0;
volatile int lampWhite = 0;
volatile int lampMode = 0;
volatile int lampSpeed = 0;
volatile unsigned long lastStatusMs = 0;
volatile bool statusReceived = false;

// Address from your working Linux Python script
static NimBLEAddress lampAddress(
    std::string("C2:14:20:00:C4:0E"),
    0
);

static const char* WRITE_UUID =
    "0000ffd9-0000-1000-8000-00805f9b34fb";

static const char* NOTIFY_UUID =
    "0000ffd4-0000-1000-8000-00805f9b34fb";

NimBLEClient* client = nullptr;
NimBLERemoteCharacteristic* writeChar = nullptr;
NimBLERemoteCharacteristic* notifyChar = nullptr;

void notificationCallback(
    NimBLERemoteCharacteristic* characteristic,
    uint8_t* data,
    size_t length,
    bool isNotify
) {
  Serial.print("BLE notification: ");

  // Parse the lamp's status packet.
  // Expected example:
  // 66 E3 23 41 20 10 FF 00 00 00 03 99
  if (length >= 10) {
    lampOn = (data[2] == 0x23);
    lampMode = data[3];
    lampSpeed = data[5];

    lampR = data[6];
    lampG = data[7];
    lampB = data[8];
    lampWhite = data[9];

    lastStatusMs = millis();
    statusReceived = true;
  }

  for (size_t i = 0; i < length; i++) {
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }

  Serial.println();
}

const char LED_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>LED Control</title>
<style>
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 24px;
    font: 16px system-ui, sans-serif;
    background: #111318; color: #eee;
  }
  main {
    max-width: 380px; margin: 30px auto;
    padding: 24px; border-radius: 18px;
    background: #20232b;
  }
  h2 { margin-top: 0; }
  .modes { display: flex; gap: 8px; margin-bottom: 24px; }
  button, input {
    font: inherit; border-radius: 9px;
  }
  .modes button {
    flex: 1; padding: 10px; border: 0;
    background: #343844; color: #eee;
  }
  .modes button.active, #apply {
    background: #7c83ff; color: white;
  }
  .field { margin: 18px 0; }
  .field label {
    display: flex; justify-content: space-between;
    margin-bottom: 8px;
  }
  input[type=range] { width: 100%; accent-color: #8c92ff; }
  input[type=number] {
    width: 76px; padding: 6px;
    background: #111318; color: white;
    border: 1px solid #454958;
  }
  #preview {
    height: 65px; border-radius: 12px;
    margin: 20px 0; background: #000;
    border: 1px solid #454958;
  }
  #apply {
    width: 100%; padding: 12px;
    border: 0; cursor: pointer;
  }
  #status { color: #b8bbc8; font-size: 14px; }
</style>
</head>
<body>
<main>
  <h2>LED Control</h2>
  <div class="modes">
    <button id="rgbMode" class="active" onclick="setMode('rgb')">RGB</button>
    <button id="whiteMode" onclick="setMode('white')">White</button>
  </div>

  <section id="rgbFields">
    <div class="field">
      <label>Red <input id="rNum" type="number" min="0" max="255" value="255"></label>
      <input id="r" type="range" min="0" max="255" value="255">
    </div>
    <div class="field">
      <label>Green <input id="gNum" type="number" min="0" max="255" value="0"></label>
      <input id="g" type="range" min="0" max="255" value="0">
    </div>
    <div class="field">
      <label>Blue <input id="bNum" type="number" min="0" max="255" value="0"></label>
      <input id="b" type="range" min="0" max="255" value="0">
    </div>
  </section>

  <section id="whiteFields" hidden>
    <div class="field">
      <label>White intensity <input id="wNum" type="number" min="0" max="255" value="255"></label>
      <input id="w" type="range" min="0" max="255" value="255">
    </div>
  </section>

  <div id="preview"></div>
  <button id="apply" onclick="applyColor()">Apply</button>
  <p id="status">Ready</p>
</main>

<script>
let mode = 'rgb';

function value(id) {
  return Math.max(0, Math.min(255,
    Number(document.getElementById(id).value) || 0));
}

function sync(id, source) {
  const v = value(source);
  document.getElementById(id).value = v;
  document.getElementById(source).value = v;
  updatePreview();
}

['r', 'g', 'b', 'w'].forEach(id => {
  document.getElementById(id).addEventListener('input', () =>
    sync(id + 'Num', id));
  document.getElementById(id + 'Num').addEventListener('input', () =>
    sync(id, id + 'Num'));
});

function setMode(next) {
  mode = next;
  document.getElementById('rgbFields').hidden = mode !== 'rgb';
  document.getElementById('whiteFields').hidden = mode !== 'white';
  document.getElementById('rgbMode').classList.toggle('active', mode === 'rgb');
  document.getElementById('whiteMode').classList.toggle('active', mode === 'white');
  updatePreview();
}

function updatePreview() {
  const c = mode === 'rgb'
    ? [value('r'), value('g'), value('b')]
    : [value('w'), value('w'), value('w')];

  document.getElementById('preview').style.background =
    `rgb(${c.join(',')})`;
}

async function applyColor() {
  const payload = mode === 'rgb'
    ? { r: value('r'), g: value('g'), b: value('b') }
    : { white: value('w') };

  const status = document.getElementById('status');
  status.textContent = 'Sending...';

  try {
    const response = await fetch('/led/color', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });

    if (!response.ok) throw new Error('HTTP ' + response.status);
    status.textContent = 'Color applied successfully';
  } catch (e) {
    status.textContent = 'Error: ' + e.message;
  }
}

updatePreview();
</script>
</body>
</html>
)rawliteral";

bool sendCommand(const uint8_t* data, size_t length) {
  if (!client || !client->isConnected() || !writeChar) {
    Serial.println("Not connected to lamp!");
    return false;
  }

  bool success = writeChar->writeValue(
      data, length, false
  );

  Serial.print("Sent: ");
  for (size_t i = 0; i < length; i++) {
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }

  Serial.println(success ? " OK" : " FAILED");
  return success;
}

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t command[] = {
    0x56, r, g, b, 0x00, 0xF0, 0xAA
  };

  sendCommand(command, sizeof(command));
}

void powerOn() {
  const uint8_t command[] = {0xCC, 0x23, 0x33};
  sendCommand(command, sizeof(command));
}

void powerOff() {
  const uint8_t command[] = {0xCC, 0x24, 0x33};
  sendCommand(command, sizeof(command));
}

void requestStatus() {
  const uint8_t command[] = {0xEF, 0x01, 0x77};
  sendCommand(command, sizeof(command));
}

bool connectToLamp() {
  Serial.println("Connecting to lamp...");

  client = NimBLEDevice::createClient();

  if (!client->connect(lampAddress)) {
    Serial.println("Connection failed.");
    return false;
  }

  Serial.println("Connected! Discovering characteristics...");

  // Discover all services and locate the characteristic
  auto services = client->getServices(true);

  for (auto* service : services) {
    auto characteristics = service->getCharacteristics(true);

    for (auto* characteristic : characteristics) {
      std::string uuid = characteristic->getUUID().toString();

      if (uuid.find("ffd9") != std::string::npos ||
          uuid.find("FFD9") != std::string::npos) {
        writeChar = characteristic;
      }

      if (uuid.find("ffd4") != std::string::npos ||
          uuid.find("FFD4") != std::string::npos) {
        notifyChar = characteristic;
      }
    }
  }

  if (!writeChar) {
    Serial.println("Write characteristic FFD9 not found.");
    return false;
  }

  Serial.println("Write characteristic found.");

  if (notifyChar && notifyChar->canNotify()) {
    if (notifyChar->subscribe(true, notificationCallback)) {
      Serial.println("Status notifications enabled.");
    } else {
      Serial.println("Could not enable notifications.");
    }
  }

  delay(300);
  requestStatus();

  return true;
}

bool getJsonInt(const String& body, const char* key, int& value) {
  String search = String("\"") + key + "\"";
  int pos = body.indexOf(search);

  if (pos < 0) return false;

  pos = body.indexOf(':', pos + search.length());
  if (pos < 0) return false;

  pos++;
  while (pos < body.length() &&
         (body[pos] == ' ' || body[pos] == '\t' ||
          body[pos] == '\r' || body[pos] == '\n')) {
    pos++;
  }

  const char* start = body.c_str() + pos;
  char* end = nullptr;
  long parsed = strtol(start, &end, 10);

  if (end == start) return false;

  // The next character must be a JSON value delimiter.
  while (*end == ' ' || *end == '\t' ||
         *end == '\r' || *end == '\n') {
    end++;
  }

  if (*end != ',' && *end != '}' && *end != '\0') {
    return false;
  }

  if (parsed < 0 || parsed > 255) return false;

  value = (int)parsed;
  return true;
}

void handleColor() {
  String body = server.arg("plain");

  int r, g, b;

  if (!getJsonInt(body, "r", r) ||
      !getJsonInt(body, "g", g) ||
      !getJsonInt(body, "b", b)) {
    server.send(400, "application/json",
                "{\"error\":\"Expected RGB values from 0 to 255\"}");
    return;
  }

  setColor((uint8_t)r, (uint8_t)g, (uint8_t)b);

  String response = "{\"sent\":true,\"r\":" + String(r) +
                    ",\"g\":" + String(g) +
                    ",\"b\":" + String(b) + "}";

  server.send(200, "application/json", response);
}

void handleOn() {
  const uint8_t command[] = {0xCC, 0x23, 0x33};
  sendCommand(command, sizeof(command));

  server.send(200, "application/json",
              "{\"sent\":true,\"command\":\"on\"}");
}

void handleOff() {
  const uint8_t command[] = {0xCC, 0x24, 0x33};
  sendCommand(command, sizeof(command));

  server.send(200, "application/json",
              "{\"sent\":true,\"command\":\"off\"}");
}

void handleStatus() {
  // Ask the lamp for an updated status notification.
  const uint8_t command[] = {0xEF, 0x01, 0x77};
  sendCommand(command, sizeof(command));

  // This is the latest notification received so far.
  bool received = statusReceived;
  bool on = lampOn;
  int r = lampR, g = lampG, b = lampB;
  int white = lampWhite, mode = lampMode, speed = lampSpeed;
  unsigned long age = received ? millis() - lastStatusMs : 0;

  String response = "{";
  response += "\"received\":" + String(received ? "true" : "false");
  response += ",\"on\":" + String(on ? "true" : "false");
  response += ",\"rgb\":{\"r\":" + String(r) +
              ",\"g\":" + String(g) +
              ",\"b\":" + String(b) + "}";
  response += ",\"white\":" + String(white);
  response += ",\"mode\":" + String(mode);
  response += ",\"speed\":" + String(speed);
  response += ",\"age_ms\":" + String(age);
  response += "}";

  server.send(200, "application/json", response);
}

void setupHttpServer() {
  server.on("/led/color", HTTP_POST, handleColor);
  server.on("/led/on", HTTP_GET, handleOn);
  server.on("/led/off", HTTP_GET, handleOff);
  server.on("/led/status", HTTP_GET, handleStatus);
  server.on("/led", HTTP_GET, []() {
    server.send_P(200, "text/html", LED_PAGE);
  });

  server.onNotFound([]() {
    server.send(404, "application/json",
                "{\"error\":\"Not found\"}");
  });

  server.begin();
  Serial.println("HTTP server started");
}

void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  r = Red");
  Serial.println("  g = Green");
  Serial.println("  b = Blue");
  Serial.println("  w = White");
  Serial.println("  y = Yellow");
  Serial.println("  p = Purple");
  Serial.println("  n = Power on");
  Serial.println("  f = Power off");
  Serial.println("  s = Request status");
  Serial.println("  ? = Show commands");
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  neopixelWrite(48, 0, 0, 255);

  Serial.println("\nESP32-S3 BLE lamp test");

  NimBLEDevice::init("ESP32-Lamp-Test");

  // Moderate transmit power; no Wi-Fi needed for this test
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);

  if (!connectToLamp()) {
    Serial.println("Setup failed. Check the address and lamp.");
    return;
  }

  printHelp();

  //WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  WiFi.setSleep(true);
  Serial.println();
  Serial.print("ESP32 IP address: ");
  Serial.println(WiFi.localIP());

  setupHttpServer();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();

    switch (c) {
      case 'r': setColor(255, 0, 0); break;
      case 'g': setColor(0, 255, 0); break;
      case 'b': setColor(0, 0, 255); break;
      case 'w': setColor(255, 255, 255); break;
      case 'y': setColor(255, 255, 0); break;
      case 'p': setColor(255, 0, 255); break;
      case 'n': powerOn(); break;
      case 'f': powerOff(); break;
      case 's': requestStatus(); break;
      case '?': printHelp(); break;
      case '\r':
      case '\n':
      case ' ': break;
      default:
        Serial.println("Unknown command. Enter ? for help.");
        break;
    }
  }

  server.handleClient();

  delay(10);
}
