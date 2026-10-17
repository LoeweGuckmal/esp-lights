# esp-lights

ESP32-S3 firmware that controls the lights in my room over Wi-Fi.

- **LED lamp** – a Bluetooth (BLE) RGB lamp, controlled through an HTTP API and a small web page
- **Room light** – coming soon

## Requirements

- ESP32-S3 DevKit (board **ESP32S3 Dev Module**). The onboard RGB LED (GPIO 48) turns blue on boot.
- [Arduino IDE 2](https://www.arduino.cc/en/software) or `arduino-cli`
- Board package **esp32** by Espressif, 3.x (built with 3.3.12)
- Library **NimBLE-Arduino** 2.x (built with 2.5.1)

## Setup

1. Copy `lights/secrets.example.h` to `lights/secrets.h` and enter your Wi-Fi name and password.
   `secrets.h` is in `.gitignore`, so your password never gets committed.
2. Check `lights/config.h`, especially the BLE address of the LED lamp.
3. Open `lights/lights.ino` in the Arduino IDE, select **ESP32S3 Dev Module** and the right port, and upload.
4. Open the serial monitor at 115200 baud. Once Wi-Fi is connected it prints the IP address.
5. Open `http://<ip>/led` in a browser.

With `arduino-cli` (run from the repository root):

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3 lights
```

```bash
arduino-cli upload --fqbn esp32:esp32:esp32s3 -p COM3 lights
```

## Project structure

```
lights/
├── lights.ino           setup() and loop(): starts all modules, nothing else
├── config.h             pins, BLE device name, HTTP port, lamp address
├── secrets.h            Wi-Fi credentials (not in git, copy from secrets.example.h)
├── secrets.example.h    template for secrets.h
├── wifi_connection.*    connects to Wi-Fi
├── http_server.*        the shared web server and its 404 handler
├── json_utils.*         small helper to read numbers from JSON request bodies
├── serial_console.*     single-character test commands over the serial monitor
├── led_lamp.*           LED lamp driver: BLE connection, commands, status (namespace LedLamp)
├── led_http.*           HTTP routes under /led
└── led_page.h           HTML control page served at /led
```

The rule of thumb: each light has a **driver** (talks to the device) and an **HTTP file**
(turns requests into driver calls). Shared things like Wi-Fi and the web server know nothing
about any specific light.

All files sit directly in the sketch folder because the Arduino IDE only shows (and compiles
without extra include paths) files at the top level of the sketch.

## HTTP API – LED lamp

| Method | Path          | Body                          | Description                                |
| ------ | ------------- | ----------------------------- | ------------------------------------------ |
| GET    | `/led`        |                               | Web control page                           |
| POST   | `/led/color`  | `{"r":255,"g":120,"b":0}`     | Set the color, each value 0–255            |
| GET    | `/led/on`     |                               | Turn the lamp on                           |
| GET    | `/led/off`    |                               | Turn the lamp off                          |
| GET    | `/led/status` |                               | Last status the lamp reported (see below)  |

Example:

```bash
curl -X POST http://<ip>/led/color -H "Content-Type: application/json" -d '{"r":255,"g":120,"b":0}'
```

`/led/status` asks the lamp for a fresh status and immediately returns the **last one received**.
`age_ms` tells you how old it is; call it again to get the newer values.

```json
{"received":true,"on":true,"rgb":{"r":255,"g":0,"b":0},"white":0,"mode":65,"speed":16,"age_ms":1200}
```

All errors are returned as JSON, e.g. `{"error":"Not found"}` with status 404.

## Serial commands

Send a single character in the serial monitor (115200 baud):

| Key | Action          | Key | Action         |
| --- | --------------- | --- | -------------- |
| `r` | Red             | `n` | Power on       |
| `g` | Green           | `f` | Power off      |
| `b` | Blue            | `s` | Request status |
| `w` | White           | `?` | Show commands  |
| `y` | Yellow          |     |                |
| `p` | Purple          |     |                |

## Adding another light (e.g. the room light)

1. **Driver** – `room_light.h` / `room_light.cpp` with a `namespace RoomLight { ... }`
   containing functions like `begin()`, `powerOn()`, `powerOff()`.
   The namespace keeps names from clashing with `LedLamp::powerOn()` and so on.
2. **HTTP routes** – `room_light_http.h` / `room_light_http.cpp` with
   `void registerRoomLightRoutes(WebServer& server);`, using paths under `/room/...`.
   Use `getJsonInt()` from `json_utils.h` to read request bodies (it takes an optional min/max range).
3. **Web page** (optional) – `room_light_page.h`, like `led_page.h`.
4. **Settings** – add pins, addresses etc. to `config.h`.
5. **Wire it up in `lights.ino`** – call `RoomLight::begin()` in `setup()` and
   `registerRoomLightRoutes(httpServer());` before `startHttpServer()`.
6. **Serial commands** (optional) – add cases in `serial_console.cpp`.

## Known limitations

- If the LED lamp can't be reached at boot, `setup()` stops before Wi-Fi and the web server start.
  Restart the ESP once the lamp is on. A dropped connection is not re-established automatically.
- The **White** mode on the `/led` page sends `{"white":…}`, which `/led/color` does not accept yet
  (it answers with 400).
