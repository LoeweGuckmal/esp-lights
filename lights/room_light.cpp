#include "room_light.h"

#include <NimBLEDevice.h>
#include <esp_random.h>
#include <mbedtls/aes.h>

#include "config.h"
#include "secrets.h"

static_assert(sizeof(ROOM_LIGHT_MESH_NAME) <= 17, "mesh name: max 16 characters");
static_assert(sizeof(ROOM_LIGHT_MESH_PASSWORD) <= 17, "mesh password: max 16 characters");

namespace RoomLight {
namespace {

// Command codes, each followed by its data bytes
constexpr uint8_t CMD_POWER = 0xD0;              // 0 = off, 1 = on
constexpr uint8_t CMD_COLOR = 0xE2;              // 0x04, r, g, b
constexpr uint8_t CMD_COLOR_BRIGHTNESS = 0xF2;   // 0x0A..0x64
constexpr uint8_t CMD_WHITE_TEMPERATURE = 0xF0;  // 0..0x7F
constexpr uint8_t CMD_WHITE_BRIGHTNESS = 0xF1;   // 1..0x7F

const NimBLEUUID PAIR_UUID("00010203-0405-0607-0809-0a0b0c0d1914");
const NimBLEUUID COMMAND_UUID("00010203-0405-0607-0809-0a0b0c0d1912");
const NimBLEUUID STATUS_UUID("00010203-0405-0607-0809-0a0b0c0d1911");

NimBLEAddress lampAddress(std::string(ROOM_LIGHT_ADDRESS), BLE_ADDR_PUBLIC);

// Created once in begin() and reused for every connection attempt.
NimBLEClient* client = nullptr;
NimBLERemoteCharacteristic* pairChar = nullptr;
NimBLERemoteCharacteristic* commandChar = nullptr;
NimBLERemoteCharacteristic* statusChar = nullptr;

// Connection state, same scheme as LedLamp: the volatile flags are set from
// the BLE task by LampCallbacks and handled in loop().
volatile bool attemptPending = false;  // a connection attempt is running
volatile bool justConnected = false;   // connected, pairing still to do
volatile bool ready = false;           // paired, commands can be sent
unsigned long lastAttemptMs = 0;
bool attemptedOnce = false;

// Key for this connection, derived while pairing
uint8_t sessionKey[16];

// Latest known state: from status packets and from the commands we sent
State state = {-1, -1, -1, -1, -1, -1, -1, -1};

// --- Encryption (packetutils.py) ---

// AES-128-ECB, but the lamp expects key, input and output byte-reversed.
void encrypt(const uint8_t key[16], const uint8_t value[16], uint8_t out[16]) {
  uint8_t k[16], in[16], result[16];
  for (int i = 0; i < 16; i++) {
    k[i] = key[15 - i];
    in[i] = value[15 - i];
  }

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, k, 128);
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, in, result);
  mbedtls_aes_free(&aes);

  for (int i = 0; i < 16; i++) out[i] = result[15 - i];
}

// Payloads are never longer than 15 bytes, so one AES block is enough
// for both helpers below.
void makeChecksum(const uint8_t nonce[8], const uint8_t* payload, size_t length,
                  uint8_t check[16]) {
  uint8_t block[16] = {0};
  memcpy(block, nonce, 8);
  block[8] = length;
  encrypt(sessionKey, block, check);

  memset(block, 0, sizeof(block));
  memcpy(block, payload, length);
  for (int i = 0; i < 16; i++) block[i] ^= check[i];
  encrypt(sessionKey, block, check);
}

// Encrypts or decrypts (same operation) the payload in place.
void cryptPayload(const uint8_t nonce[8], uint8_t* payload, size_t length) {
  uint8_t base[16] = {0};
  memcpy(base + 1, nonce, 8);

  uint8_t stream[16];
  encrypt(sessionKey, base, stream);
  for (size_t i = 0; i < length; i++) payload[i] ^= stream[i];
}

// --- Protocol ---

// Builds the 20-byte encrypted command packet and writes it.
// data may be at most 10 bytes.
bool sendCommand(uint8_t command, const uint8_t* data, size_t length) {
  if (!isConnected()) {
    Serial.println("Room light not connected!");
    return false;
  }

  // Sequence number: random, it only has to change between packets.
  uint8_t packet[20];
  esp_fill_random(packet, 3);

  // Nonce: first 4 bytes of the reversed MAC, 0x01, sequence number
  const uint8_t* mac = lampAddress.getVal();  // already reversed
  uint8_t nonce[8];
  memcpy(nonce, mac, 4);
  nonce[4] = 0x01;
  memcpy(nonce + 5, packet, 3);

  // Payload: destination mesh id 0 (2 bytes), command, 0x60 0x01, data
  uint8_t payload[15] = {0};
  payload[2] = command;
  payload[3] = 0x60;
  payload[4] = 0x01;
  memcpy(payload + 5, data, length);

  uint8_t check[16];
  makeChecksum(nonce, payload, sizeof(payload), check);
  cryptPayload(nonce, payload, sizeof(payload));

  packet[3] = check[0];
  packet[4] = check[1];
  memcpy(packet + 5, payload, sizeof(payload));

  bool success = commandChar->writeValue(packet, sizeof(packet), true);
  Serial.printf("Room light command %02X: %s\n", command, success ? "OK" : "FAILED");
  return success;
}

bool sendByte(uint8_t command, uint8_t value) {
  return sendCommand(command, &value, 1);
}

// Proves we know the mesh name and password and derives the session key.
bool pair() {
  uint8_t name[16] = {0};
  uint8_t password[16] = {0};
  memcpy(name, ROOM_LIGHT_MESH_NAME, strlen(ROOM_LIGHT_MESH_NAME));
  memcpy(password, ROOM_LIGHT_MESH_PASSWORD, strlen(ROOM_LIGHT_MESH_PASSWORD));

  uint8_t namePass[16];
  for (int i = 0; i < 16; i++) namePass[i] = name[i] ^ password[i];

  // Our 8 random bytes, followed later by the lamp's 8 random bytes
  uint8_t sessionRandom[16] = {0};
  esp_fill_random(sessionRandom, 8);

  uint8_t proof[16];
  encrypt(sessionRandom, namePass, proof);

  uint8_t request[17];
  request[0] = 0x0C;
  memcpy(request + 1, sessionRandom, 8);
  memcpy(request + 9, proof, 8);

  if (!pairChar->writeValue(request, sizeof(request), true)) return false;

  // Turns on status updates in the lamp.
  const uint8_t enableStatus = 0x01;
  statusChar->writeValue(&enableStatus, 1, true);

  NimBLEAttValue reply = pairChar->readValue();

  if (reply.size() >= 9 && reply.data()[0] == 0x0D) {
    memcpy(sessionRandom + 8, reply.data() + 1, 8);
    encrypt(namePass, sessionRandom, sessionKey);
    return true;
  }

  if (reply.size() > 0 && reply.data()[0] == 0x0E) {
    Serial.println("Room light: wrong mesh name or password.");
  } else {
    Serial.println("Room light: unexpected pair reply.");
  }
  return false;
}

// Decrypts a 20-byte status packet and updates `state`.
void parseStatus(const uint8_t* data, size_t length) {
  if (length < 20) return;

  uint8_t packet[20];
  memcpy(packet, data, sizeof(packet));

  // Nonce: first 3 bytes of the reversed MAC, packet bytes 0..4
  uint8_t nonce[8];
  memcpy(nonce, lampAddress.getVal(), 3);
  memcpy(nonce + 3, packet, 5);

  uint8_t* payload = packet + 7;
  const size_t payloadLength = sizeof(packet) - 7;
  cryptPayload(nonce, payload, payloadLength);

  uint8_t check[16];
  makeChecksum(nonce, payload, payloadLength, check);
  if (check[0] != packet[5] || check[1] != packet[6]) {
    Serial.println("Room light: status checksum mismatch.");
    return;
  }

  // Larger values are other kinds of packets.
  int mode = packet[12];
  if (mode >= 40) return;

  state.mode = mode;
  state.status = mode % 2;
  state.whiteBrightness = packet[13];
  state.whiteTemp = packet[14];
  state.colorBrightness = packet[15];
  state.r = packet[16];
  state.g = packet[17];
  state.b = packet[18];
}

// --- Connection (same scheme as led_lamp.cpp) ---

// Runs in the BLE task: only set flags here.
class LampCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pClient) override {
    rgbLedWrite(STATUS_LED_PIN, 0, 0, 0);
    attemptPending = false;
    justConnected = true;
  }

  void onConnectFail(NimBLEClient* pClient, int reason) override {
    attemptPending = false;
    Serial.print("Room light connection failed, reason ");
    Serial.println(reason);
  }

  void onDisconnect(NimBLEClient* pClient, int reason) override {
    ready = false;
    justConnected = false;
    attemptPending = false;
    rgbLedWrite(STATUS_LED_PIN, 255, 0, 0);
    Serial.print("Room light disconnected, reason ");
    Serial.println(reason);
  }
};

LampCallbacks callbacks;

void startConnecting() {
  Serial.println("Connecting to room light...");

  attemptedOnce = true;
  lastAttemptMs = millis();

  // connect() deletes the old services, so drop our pointers into them first.
  pairChar = nullptr;
  commandChar = nullptr;
  statusChar = nullptr;

  attemptPending = true;
  if (!client->connect(lampAddress, true, true)) {
    attemptPending = false;
    // Usually the LED lamp is connecting right now (only one attempt can
    // run at a time), so try again in a second.
    lastAttemptMs = millis() - ROOM_LIGHT_RETRY_INTERVAL_MS + 1000;
    Serial.println("Could not start connecting to room light.");
  }
}

// Called once after each new connection. Blocks for a moment.
bool setupConnection() {
  Serial.println("Room light connected, pairing...");

  for (auto* service : client->getServices(true)) {
    for (auto* characteristic : service->getCharacteristics(true)) {
      const NimBLEUUID& uuid = characteristic->getUUID();

      if (uuid == PAIR_UUID) pairChar = characteristic;
      if (uuid == COMMAND_UUID) commandChar = characteristic;
      if (uuid == STATUS_UUID) statusChar = characteristic;
    }
  }

  if (!pairChar || !commandChar || !statusChar) {
    Serial.println("Room light characteristics not found.");
    return false;
  }

  if (!pair() || !client->isConnected()) {
    Serial.println("Room light pairing failed.");
    return false;
  }

  ready = true;
  Serial.println("Room light ready.");

  readState();
  return true;
}

}  // namespace

void begin() {
  client = NimBLEDevice::createClient(lampAddress);
  client->setClientCallbacks(&callbacks, false);
  client->setConnectTimeout(ROOM_LIGHT_CONNECT_TIMEOUT_MS);
}

void loop() {
  if (!client) return;

  if (justConnected) {
    justConnected = false;
    if (!setupConnection()) {
      client->disconnect();
    }
    return;
  }

  // Safety net in case the stack never reports the end of an attempt.
  if (attemptPending &&
      millis() - lastAttemptMs > ROOM_LIGHT_CONNECT_TIMEOUT_MS + 5000) {
    attemptPending = false;
  }

  if (ready || attemptPending || client->isConnected()) return;

  if (attemptedOnce && millis() - lastAttemptMs < ROOM_LIGHT_RETRY_INTERVAL_MS) {
    return;
  }

  startConnecting();
}

bool isConnected() {
  return ready && client && client->isConnected() && commandChar;
}

bool powerOn() {
  if (!sendByte(CMD_POWER, 0x01)) return false;
  state.status = 1;
  return true;
}

bool powerOff() {
  if (!sendByte(CMD_POWER, 0x00)) return false;
  state.status = 0;
  return true;
}

bool setColor(uint8_t r, uint8_t g, uint8_t b) {
  const uint8_t data[] = {0x04, r, g, b};
  if (!sendCommand(CMD_COLOR, data, sizeof(data))) return false;
  state.mode = 3;
  state.status = 1;
  state.r = r;
  state.g = g;
  state.b = b;
  return true;
}

bool setColorBrightness(uint8_t brightness) {
  if (!sendByte(CMD_COLOR_BRIGHTNESS, brightness)) return false;
  state.colorBrightness = brightness;
  return true;
}

bool setWhiteTemperature(uint8_t temp) {
  if (!sendByte(CMD_WHITE_TEMPERATURE, temp)) return false;
  state.whiteTemp = temp;
  return true;
}

bool setWhiteBrightness(uint8_t brightness) {
  if (!sendByte(CMD_WHITE_BRIGHTNESS, brightness)) return false;
  state.whiteBrightness = brightness;
  return true;
}

State readState() {
  if (isConnected()) {
    NimBLEAttValue value = statusChar->readValue();
    parseStatus(value.data(), value.size());
  }
  return state;
}

}  // namespace RoomLight
