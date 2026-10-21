#include "presence.h"

#include <NimBLEDevice.h>
#include <Preferences.h>
#include <mbedtls/aes.h>

#include "nimble/nimble/host/include/host/ble_store.h"

#include "config.h"

namespace Presence {
namespace {

// Listen SCAN_WINDOW_MS out of every SCAN_INTERVAL_MS. Wi-Fi and the lamp
// connections share the one radio, so they keep most of the airtime.
constexpr uint16_t SCAN_INTERVAL_MS = 100;
constexpr uint16_t SCAN_WINDOW_MS = 30;

// answer() trusts a finished check this long.
constexpr uint32_t RESULT_MAX_AGE_MS = 10 * 60 * 1000;

// Only there to pair: the characteristic can only be read over an encrypted
// connection, so reading it makes the phone pair if it hasn't already.
const char PAIR_SERVICE_UUID[] = "f3da5f14-5027-4a89-af74-9ce31971ff8b";
const char PAIR_CHAR_UUID[] = "abd66932-2288-4dec-89b4-8f9b94bc2fb4";

// The paired phone, saved in flash. Change the version when the layout
// changes; a saved block with another version is ignored.
constexpr uint8_t SAVED_VERSION = 1;
struct SavedPhone {
  uint8_t version;
  uint8_t irk[16];    // identity resolving key, as NimBLE stores it
  uint8_t idVal[6];   // identity address, least significant byte first
  uint8_t idType;
};

Preferences prefs;
bool isEnabled = true;
bool phoneKnown = false;
SavedPhone phone = {};

// The running or last check. The volatile values are written from the BLE
// task by ScanCallbacks.
volatile bool checking = false;
bool checkedOnce = false;
Reason checkReason = Reason::Manual;
unsigned long checkEndMs = 0;  // planned end while checking, real end after
unsigned long lastScanStartMs = 0;
volatile bool seen = false;
volatile bool found = false;
volatile int lastRssi = 0;
volatile int bestRssi = 0;
volatile unsigned long heardMs = 0;

// Pairing
NimBLEServer* server = nullptr;
bool pairingOpen = false;
bool pairingWorked = false;
unsigned long pairingEndMs = 0;
// Set from the BLE task once a phone has paired and shared its key.
volatile bool pairedPending = false;
SavedPhone pairedPhone = {};

// ah() from the Bluetooth spec (Core, Vol 3, Part H, 2.2.2): the lower half
// of a resolvable private address is a hash of the upper half (prand),
// made with the phone's IRK. NimBLE keeps keys and addresses least
// significant byte first, AES wants them the other way round.
bool resolves(const uint8_t irk[16], const uint8_t addr[6]) {
  // The top two bits 01 mark a resolvable private address.
  if ((addr[5] & 0xC0) != 0x40) return false;

  uint8_t key[16];
  for (int i = 0; i < 16; i++) key[i] = irk[15 - i];

  uint8_t block[16] = {0};
  block[13] = addr[5];
  block[14] = addr[4];
  block[15] = addr[3];

  uint8_t out[16];
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 128);
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, block, out);
  mbedtls_aes_free(&aes);

  return out[15] == addr[0] && out[14] == addr[1] && out[13] == addr[2];
}

// The sample from the spec (Core, Vol 3, Part H, D.7): IRK
// ec0234a357c8ad05341010a60a397d9b, prand 708194, hash 0dfbaa.
bool selfTest() {
  const uint8_t irk[16] = {0x9b, 0x7d, 0x39, 0x0a, 0xa6, 0x10, 0x10, 0x34,
                           0x05, 0xad, 0xc8, 0x57, 0xa3, 0x34, 0x02, 0xec};
  const uint8_t addr[6] = {0xaa, 0xfb, 0x0d, 0x94, 0x81, 0x70};
  uint8_t wrong[6];
  memcpy(wrong, addr, 6);
  wrong[0] ^= 1;
  return resolves(irk, addr) && !resolves(irk, wrong);
}

bool isMyPhone(const NimBLEAddress& address) {
  const uint8_t* val = address.getVal();
  // After pairing, the controller may resolve the address itself and
  // report the identity address instead.
  if (memcmp(val, phone.idVal, 6) == 0) return true;
  return address.getType() == BLE_ADDR_RANDOM && resolves(phone.irk, val);
}

// Runs in the BLE task for every advertisement while scanning.
class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* device) override {
    if (!checking || !phoneKnown || !isMyPhone(device->getAddress())) return;

    int rssi = device->getRSSI();
    if (!seen || rssi > bestRssi) bestRssi = rssi;
    lastRssi = rssi;
    heardMs = millis();
    seen = true;
    if (rssi >= PRESENCE_MIN_RSSI) found = true;
  }
};

ScanCallbacks scanCallbacks;

void startAdvertising() {
  long left = (long)(pairingEndMs - millis());
  if (left > 0) NimBLEDevice::getAdvertising()->start(left);
}

// Runs in the BLE task.
class PairCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    Serial.println("Presence: phone connected, asking it to pair...");
    // The phone shows a "Pair" prompt.
    NimBLEDevice::startSecurity(connInfo.getConnHandle());
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    Serial.printf("Presence: phone disconnected, reason %d\n", reason);
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    if (!connInfo.isEncrypted() || !connInfo.isBonded()) {
      Serial.println("Presence: pairing failed. Forget the ESP in the phone's "
                     "Bluetooth settings and try again.");
      return;
    }

    // NimBLE has saved the keys before calling this.
    ble_store_key_sec key = {};
    key.peer_addr = *connInfo.getIdAddress().getBase();
    ble_store_value_sec value;
    if (ble_store_read_peer_sec(&key, &value) != 0 || !value.irk_present) {
      Serial.println("Presence: paired, but the phone didn't share its key.");
      return;
    }

    pairedPhone.version = SAVED_VERSION;
    memcpy(pairedPhone.irk, value.irk, sizeof(pairedPhone.irk));
    memcpy(pairedPhone.idVal, key.peer_addr.val, sizeof(pairedPhone.idVal));
    pairedPhone.idType = key.peer_addr.type;
    pairedPending = true;
  }
};

PairCallbacks pairCallbacks;

void disconnectPhones() {
  if (!server) return;
  for (uint16_t handle : server->getPeerDevices()) server->disconnect(handle);
}

void finishCheck() {
  checking = false;
  checkEndMs = millis();
  NimBLEDevice::getScan()->stop();

  if (found) {
    Serial.printf("Presence: phone found (%d dBm)\n", bestRssi);
  } else if (seen) {
    Serial.printf("Presence: phone heard, but only at %d dBm\n", bestRssi);
  } else {
    Serial.println("Presence: phone not found");
  }
}

void savePhone(const SavedPhone& newPhone) {
  phone = newPhone;
  phoneKnown = true;
  prefs.putBytes("phone", &phone, sizeof(phone));
}

}  // namespace

void begin() {
  prefs.begin("presence", false);
  isEnabled = prefs.getBool("on", true);

  SavedPhone loaded;
  if (prefs.getBytesLength("phone") == sizeof(loaded) &&
      prefs.getBytes("phone", &loaded, sizeof(loaded)) == sizeof(loaded) &&
      loaded.version == SAVED_VERSION) {
    phone = loaded;
    phoneKnown = true;
  }

  if (!selfTest()) Serial.println("Presence: address check self-test FAILED");

  // Passive: only listen, never ask devices for more data.
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, true);  // every advertisement, for the live signal
  scan->setActiveScan(false);
  scan->setInterval(SCAN_INTERVAL_MS);
  scan->setWindow(SCAN_WINDOW_MS);
  scan->setMaxResults(0);  // only the callback, no list in memory

  server = NimBLEDevice::createServer();
  server->setCallbacks(&pairCallbacks, false);
  NimBLEService* service = server->createService(PAIR_SERVICE_UUID);
  NimBLECharacteristic* characteristic = service->createCharacteristic(
      PAIR_CHAR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC);
  characteristic->setValue("paired");
  server->start();

  NimBLEDevice::getAdvertising()->setName(BLE_DEVICE_NAME);

  Serial.printf("Presence: %s, %s\n", isEnabled ? "on" : "off",
                phoneKnown ? "phone paired" : "no phone paired");
}

void loop() {
  if (pairedPending) {
    pairedPending = false;
    savePhone(pairedPhone);
    pairingWorked = true;
    Serial.println("Presence: phone paired.");
    stopPairing();
  }

  if (pairingOpen) {
    if ((long)(millis() - pairingEndMs) >= 0) {
      Serial.println("Presence: pairing window closed.");
      stopPairing();
    } else if (!NimBLEDevice::getAdvertising()->isAdvertising() &&
               server->getConnectedCount() == 0) {
      // A phone connected and left without pairing: open up again.
      startAdvertising();
    }
  }

  if (!checking) return;

  if ((long)(millis() - checkEndMs) >= 0 ||
      (found && checkReason != Reason::Watch)) {
    finishCheck();
    return;
  }

  // A lamp connecting stops the scan, and no scan can start while one
  // connects. Keep trying until the time is up.
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (!scan->isScanning() && millis() - lastScanStartMs > 500) {
    lastScanStartMs = millis();
    scan->start(checkEndMs - millis(), false, true);
  }
}

bool enabled() {
  return isEnabled;
}

void setEnabled(bool on) {
  isEnabled = on;
  prefs.putBool("on", on);
  if (!on) {
    stopCheck();
    stopPairing();
  }
}

bool hasPhone() {
  return phoneKnown;
}

void forgetPhone() {
  stopCheck();
  if (phoneKnown) {
    ble_addr_t id;
    id.type = phone.idType;
    memcpy(id.val, phone.idVal, sizeof(id.val));
    NimBLEDevice::deleteBond(NimBLEAddress(id));
  }
  phoneKnown = false;
  phone = {};
  checkedOnce = false;
  prefs.remove("phone");
  Serial.println("Presence: phone forgotten.");
}

const char* check(Reason reason) {
  if (!isEnabled) return "Presence checks are off";
  if (!phoneKnown) return "Pair your phone first";

  checking = false;
  NimBLEDevice::getScan()->stop();

  seen = false;
  found = false;
  lastRssi = 0;
  bestRssi = 0;
  checkReason = reason;
  uint32_t seconds = reason == Reason::Watch ? PRESENCE_WATCH_S : PRESENCE_SCAN_S;
  checkEndMs = millis() + seconds * 1000;
  lastScanStartMs = millis() - 1000;  // loop() starts the scan right away
  checkedOnce = true;
  checking = true;

  Serial.println("Presence: looking for the phone...");
  return nullptr;
}

void stopCheck() {
  if (checking) finishCheck();
}

Answer answer() {
  if (!isEnabled || !phoneKnown || !checkedOnce) return Answer::Unknown;
  if (checking) return Answer::Checking;
  if (millis() - checkEndMs > RESULT_MAX_AGE_MS) return Answer::Unknown;
  return found ? Answer::Present : Answer::Away;
}

Result result() {
  Result r = {};
  r.valid = checkedOnce;
  r.running = checking;
  r.reason = checkReason;
  r.seen = seen;
  r.found = found;
  r.rssi = lastRssi;
  r.bestRssi = bestRssi;
  // heardMs first: the BLE task may set it to a later time meanwhile.
  unsigned long heard = heardMs;
  r.heardAgoS = seen ? (millis() - heard) / 1000 : 0;
  if (checking) {
    long left = (long)(checkEndMs - millis());
    r.leftS = left > 0 ? (left + 999) / 1000 : 0;
  } else {
    r.ageS = (millis() - checkEndMs) / 1000;
  }
  return r;
}

const char* startPairing() {
  if (!isEnabled) return "Turn presence on first";
  if (!server) return "Bluetooth is not ready";

  // Bond (keep the keys), no passkey: the ESP has no screen or keypad.
  // Both sides share their identity keys; the phone's is what we're after.
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

  pairingEndMs = millis() + PRESENCE_PAIR_WINDOW_S * 1000;
  pairingOpen = true;
  pairingWorked = false;
  startAdvertising();
  if (!NimBLEDevice::getAdvertising()->isAdvertising()) {
    pairingOpen = false;
    return "Could not start advertising";
  }

  Serial.printf("Presence: ready to pair as \"%s\" for %lu s\n", BLE_DEVICE_NAME,
                (unsigned long)PRESENCE_PAIR_WINDOW_S);
  return nullptr;
}

void stopPairing() {
  pairingOpen = false;
  NimBLEDevice::stopAdvertising();
  disconnectPhones();
}

uint32_t pairingLeftS() {
  if (!pairingOpen) return 0;
  long left = (long)(pairingEndMs - millis());
  return left > 0 ? (left + 999) / 1000 : 1;
}

bool lastPairingWorked() {
  return pairingWorked;
}

}  // namespace Presence
