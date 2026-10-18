# esp-lights

ESP32-S3 firmware that controls the lights in my room over Wi-Fi.

- **LED strip** – a Bluetooth (BLE) RGB LED strip under the bed, for ambient light
- **Room light** – an AwoX mesh bulb (BLE), ported from my Python `awoxmeshlight` project

Both are controlled through the same kind of HTTP API and a small web page each.
On top of that, **wake-up alarms** switch the room light on with a sunrise fade, set per
weekday like the sleep schedule on the iPhone.

## Requirements

- ESP32-S3 DevKit (board **ESP32S3 Dev Module**). The onboard RGB LED (GPIO 48) is switched off on boot.
- [Arduino IDE 2](https://www.arduino.cc/en/software) or `arduino-cli`
- Board package **esp32** by Espressif, 3.x (built with 3.3.12)
- Library **NimBLE-Arduino** 2.x (built with 2.5.1)

## Setup

1. Copy `lights/secrets.example.h` to `lights/secrets.h` and enter your Wi-Fi name and password,
   and the mesh name and password of the room light (`MESH_NAME` / `MESH_PASSWORD` from the
   Python project). `secrets.h` is in `.gitignore`, so your passwords never get committed.
2. Check `lights/config.h`, especially the BLE addresses of both lights.
3. Open `lights/lights.ino` in the Arduino IDE, select **ESP32S3 Dev Module** and the right port, and upload.
4. Open the serial monitor at 115200 baud. Once Wi-Fi is connected it prints the IP address.
5. Open `http://<ip>/led`, `http://<ip>/room` or `http://<ip>/alarm` in a browser.
6. For the alarms, check `TIME_ZONE` in `config.h` (default: Central Europe with daylight saving).

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
├── config.h             pins, BLE device name, HTTP port, light addresses
├── secrets.h            Wi-Fi and mesh credentials (not in git, copy from secrets.example.h)
├── secrets.example.h    template for secrets.h
├── wifi_connection.*    connects to Wi-Fi
├── http_server.*        the shared web server and its 404 handler
├── http_utils.*         read request parameters, send JSON answers
├── light_page.*         HTML control page shared by all lights
├── serial_console.*     single-character test commands over the serial monitor
├── led_lamp.*           LED strip driver: BLE connection, commands, status (namespace LedLamp)
├── led_http.*           HTTP routes under /led
├── room_light.*         room light driver: BLE connection, pairing, encryption (namespace RoomLight)
├── room_light_http.*    HTTP routes under /room
├── alarm_clock.*        alarms: time sync, schedules saved in flash, sunrise (namespace AlarmClock)
├── alarm_http.*         HTTP routes under /alarm
└── alarm_page.*         the alarm page
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

### LED strip – `/led`

| Path          | Parameters                                                          |
| ------------- | ------------------------------------------------------------------- |
| `/led`        | web control page (GET)                                              |
| `/led/on`     | –                                                                   |
| `/led/off`    | –                                                                   |
| `/led/rgb`    | `r`, `g`, `b` (0–255), optional `brightness` (1–100, scales the color) |
| `/led/white`  | optional `brightness` (1–100, default 70)                           |
| `/led/status` | –                                                                   |

`/led/status` asks the strip for a fresh status and immediately returns the **last one received**.
`age_ms` tells you how old it is; call it again to get the newer values. While `connected` is
`false`, the values are the last ones received before the connection dropped (or zeros if there
never was one).

```json
{"ok":true,"connected":true,"state":{"received":true,"is_on":true,"mode":65,"red":255,"green":0,"blue":0,"white":0,"speed":16,"age_ms":1200}}
```

### Alarms – `/alarm`

Wake-up alarms for the room light. The page at `/alarm` shows the next alarm at the top, where
you can change or skip it **for that day only**, and the weekly schedules below it.

- A **schedule** has a time and one or more weekdays. Each weekday belongs to one schedule at
  most; saving a schedule with a day of another one moves the day over (and deletes the other
  schedule if it has no days left).
- **Sunrise:** `fade` minutes before the alarm time the light comes on at its lowest brightness
  and warmest white, then fades to the schedule's `brightness` and `temperature` by the alarm
  time. `fade` 0 switches it on at the alarm time.
- **Next alarm only:** one day can have a different time or be skipped. On a day without a
  schedule, this is a one-time alarm. It is dropped once the day is over.
- If the room light is already on when the sunrise would start, the alarm leaves it alone.
  Switching the room light off or setting a color or white (`/room/off`, `/rgb`, `/white`)
  ends a running sunrise.

| Path                | Parameters                                                                 |
| ------------------- | -------------------------------------------------------------------------- |
| `/alarm`            | alarm page (GET)                                                           |
| `/alarm/status`     | –                                                                          |
| `/alarm/on`         | – (master switch: alarms ring)                                             |
| `/alarm/off`        | – (no alarm rings, schedules are kept)                                     |
| `/alarm/save`       | `id` (missing = new schedule), `days`, `hour`, `minute`, `fade` (0–60), `brightness` (1–100), `temperature` (0–100), `enabled` (0/1) |
| `/alarm/delete`     | `id`                                                                       |
| `/alarm/next`       | `hour` and `minute`, or `skip=1`; optional `date` (`yyyymmdd`, default: the next alarm) |
| `/alarm/next/reset` | – (back to the schedule)                                                   |
| `/alarm/stop`       | – (ends a running sunrise, the light stays as it is)                       |

`days` is a bit mask: Sunday = 1, Monday = 2, Tuesday = 4 … Saturday = 64, so weekdays are 62
and the weekend is 65. When changing a schedule, parameters you leave out keep their value.
`id` is the position in `schedules`, which is sorted by the first day (Monday first).

Every route answers with the full status. Times are in the ESP's time zone, dates are `yyyymmdd`:

```bash
curl "http://<ip>/alarm/save?days=62&hour=6&minute=45&fade=20"
```

```json
{"ok":true,"time_valid":true,"now":{"date":20261018,"weekday":0,"time":"22:13"},"enabled":true,
 "schedules":[{"id":0,"days":62,"time":"06:45","enabled":true,"fade":20,"brightness":100,"temperature":50}],
 "upcoming":[{"date":20261019,"weekday":1,"time":"06:45","schedule":0,"changed":false,"skipped":false,
              "fade":20,"brightness":100,"temperature":50,"in_minutes":512}],
 "sunrise":null}
```

`upcoming` holds the next alarm and the one after it (useful when the next one is skipped).
`sunrise` is set while one is running: `{"time":"06:45","progress":40,"lamp_connected":true}`.

## Serial commands

Send a single character in the serial monitor (115200 baud):

| Key | LED strip      | Key | LED strip      | Key | Room light   |
| --- | -------------- | --- | -------------- | --- | ------------ |
| `r` | Red            | `n` | Power on       | `N` | Power on     |
| `g` | Green          | `f` | Power off      | `F` | Power off    |
| `b` | Blue           | `s` | Request status | `S` | Print status |
| `w` | White          | `?` | Show commands  |     |              |
| `y` | Yellow         |     |                |     |              |
| `p` | Purple         |     |                |     |              |

`A` prints the ESP's time and the next alarm.

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

- If a lamp is unreachable, Wi-Fi and the web server still run. After a dropped connection the ESP
  retries right away, then waits longer after each failed attempt: 15 s, 30 s, 1 min … up to 10 min
  (`BLE_RETRY_MIN_MS` / `BLE_RETRY_MAX_MS` in `config.h`). This way it doesn't keep the radio busy
  for hours while a lamp is off at the wall switch.
- A command for a disconnected lamp (HTTP or serial) tries to connect right away. HTTP commands wait
  for it, so the first command after the lamp was off can take a few seconds. If the lamp is still
  unreachable, they answer 503 after ~7 s. The web server handles one request at a time, so other
  requests wait meanwhile. `/status` doesn't wait: it starts connecting and answers right away.
- While the ESP is connected to the room light, the AwoX app and the Python bridge can't connect to it.
- Alarms need the time from the internet (NTP). Until the ESP has it after a boot, no alarm rings and
  the alarm page says so. Alarms that were due while the ESP was off don't ring later.
- Before a sunrise the ESP starts connecting to the room light `ALARM_PRECONNECT_S` early. If the
  lamp is still unreachable, it keeps trying until `ALARM_RETRY_S` after the alarm time.
