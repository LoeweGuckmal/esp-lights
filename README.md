# esp-lights

ESP32-S3 firmware that controls the lights in my room over Wi-Fi.

- **LED lamp** – a Bluetooth (BLE) RGB lamp
- **Room light** – an AwoX mesh bulb (BLE), ported from my Python `awoxmeshlight` project

Both are controlled through the same kind of HTTP API and a small web page each.

## Requirements

- ESP32-S3 DevKit (board **ESP32S3 Dev Module**). The onboard RGB LED (GPIO 48) is switched off on boot.
- [Arduino IDE 2](https://www.arduino.cc/en/software) or `arduino-cli`
- Board package **esp32** by Espressif, 3.x (built with 3.3.12)
- Library **NimBLE-Arduino** 2.x (built with 2.5.1)

## Setup

1. Copy `lights/secrets.example.h` to `lights/secrets.h` and enter your Wi-Fi name and password,
   and the mesh name and password of the room light (`MESH_NAME` / `MESH_PASSWORD` from the
   Python project). `secrets.h` is in `.gitignore`, so your passwords never get committed.
2. Check `lights/config.h`, especially the BLE addresses of both lamps.
3. Open `lights/lights.ino` in the Arduino IDE, select **ESP32S3 Dev Module** and the right port, and upload.
4. Open the serial monitor at 115200 baud. Once Wi-Fi is connected it prints the IP address.
5. Open `http://<ip>/led` or `http://<ip>/room` in a browser.

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
├── config.h             pins, BLE device name, HTTP port, lamp addresses
├── secrets.h            Wi-Fi and mesh credentials (not in git, copy from secrets.example.h)
├── secrets.example.h    template for secrets.h
├── wifi_connection.*    connects to Wi-Fi
├── http_server.*        the shared web server and its 404 handler
├── http_utils.*         read request parameters, send JSON answers
├── light_page.*         HTML control page shared by all lights
├── serial_console.*     single-character test commands over the serial monitor
├── led_lamp.*           LED lamp driver: BLE connection, commands, status (namespace LedLamp)
├── led_http.*           HTTP routes under /led
├── room_light.*         room light driver: BLE connection, pairing, encryption (namespace RoomLight)
└── room_light_http.*    HTTP routes under /room
```

The rule of thumb: each light has a **driver** (talks to the device) and an **HTTP file**
(turns requests into driver calls). Shared things like Wi-Fi and the web server know nothing
about any specific light.

All files sit directly in the sketch folder because the Arduino IDE only shows (and compiles
without extra include paths) files at the top level of the sketch.

## HTTP API

Both lights use the same design as the AwoX Python bridge (`bridge/lamp_server.py`):

- Every endpoint accepts **GET or POST**.
- Parameters go in the query string (`?r=255&g=80&b=0`) or a JSON body (`{"r":255,"g":80,"b":0}`).
- Answers are JSON with `"ok":true`, errors are `{"ok":false,"error":"..."}`
  (400 bad parameter, 404 unknown path, 503 lamp not connected).

### Room light – `/room`

| Path           | Parameters                                                           |
| -------------- | -------------------------------------------------------------------- |
| `/room`        | web control page (GET)                                               |
| `/room/on`     | –                                                                    |
| `/room/off`    | –                                                                    |
| `/room/rgb`    | `r`, `g`, `b` (0–255) and/or `brightness` (1–100)                    |
| `/room/white`  | `temperature` (0 = cold … 100 = warm) and/or `brightness` (1–100); neither = 50 / 70 |
| `/room/status` | –                                                                    |

```bash
curl "http://<ip>/room/rgb?r=255&g=80&b=0&brightness=80"
```

`/room/status` reads a fresh status from the lamp. The values are the lamp's raw ones, like in the
bridge (`white_brightness` / `white_temp` 0–127, `color_brightness` 10–100, `mode` 1 = white,
3 = color), `null` while unknown:

```json
{"ok":true,"connected":true,"state":{"status":1,"is_on":true,"mode":3,"white_brightness":89,"white_temp":64,"color_brightness":80,"red":255,"green":80,"blue":0}}
```

The bridge's Node client (`awoxLamp.js`) works against the ESP: set
`AWOX_BRIDGE_URL=http://<ip>/room`. Only `connect()` and `disconnect()` don't exist here,
because the ESP keeps the lamp connected by itself.

### LED lamp – `/led`

| Path          | Parameters                                                          |
| ------------- | ------------------------------------------------------------------- |
| `/led`        | web control page (GET)                                              |
| `/led/on`     | –                                                                   |
| `/led/off`    | –                                                                   |
| `/led/rgb`    | `r`, `g`, `b` (0–255), optional `brightness` (1–100, scales the color) |
| `/led/white`  | optional `brightness` (1–100, default 70)                           |
| `/led/status` | –                                                                   |

`/led/status` asks the lamp for a fresh status and immediately returns the **last one received**.
`age_ms` tells you how old it is; call it again to get the newer values. While `connected` is
`false`, the values are the last ones received before the connection dropped (or zeros if there
never was one).

```json
{"ok":true,"connected":true,"state":{"received":true,"is_on":true,"mode":65,"red":255,"green":0,"blue":0,"white":0,"speed":16,"age_ms":1200}}
```

## Serial commands

Send a single character in the serial monitor (115200 baud):

| Key | LED lamp       | Key | LED lamp       | Key | Room light   |
| --- | -------------- | --- | -------------- | --- | ------------ |
| `r` | Red            | `n` | Power on       | `N` | Power on     |
| `g` | Green          | `f` | Power off      | `F` | Power off    |
| `b` | Blue           | `s` | Request status | `S` | Print status |
| `w` | White          | `?` | Show commands  |     |              |
| `y` | Yellow         |     |                |     |              |
| `p` | Purple         |     |                |     |              |

## Adding another light

Look at the room light as an example:

1. **Driver** – `<name>.h` / `<name>.cpp` with its own `namespace`, containing `begin()`,
   `loop()` and functions like `powerOn()`. The namespace keeps names from clashing.
2. **HTTP routes** – `<name>_http.h` / `.cpp` with a `register...Routes(WebServer& server)`.
   Use `readIntParam()`, `sendOk()` and `sendError()` from `http_utils.h`, and
   `sendLightPage()` for the control page.
3. **Settings** – add pins, addresses etc. to `config.h`, passwords to `secrets.h`
   (and `secrets.example.h`).
4. **Wire it up in `lights.ino`** – call `begin()` in `setup()`, `loop()` in `loop()` and the
   route registration before `startHttpServer()`. Never block in `begin()` or `loop()` for long:
   everything shares one loop.
5. **Serial commands** (optional) – add cases in `serial_console.cpp`.

## Known limitations

- If a lamp is unreachable, Wi-Fi and the web server still run. The ESP retries connecting every
  15 s (`..._RETRY_INTERVAL_MS` in `config.h`) and right away after a dropped connection, so it can
  take up to ~15 s until a lamp responds after it is switched on. Until then, commands answer 503.
- While the ESP is connected to the room light, the AwoX app and the Python bridge can't connect to it.
