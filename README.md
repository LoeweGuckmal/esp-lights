# esp-lights

ESP32-S3 firmware that controls the lights in my room over Wi-Fi.

- **LED strip** – a Bluetooth (BLE) RGB LED strip under the bed, for ambient light
- **Room light** – an AwoX mesh bulb (BLE)

Both are controlled through the same kind of HTTP API and a small web page each.
On top of that, **wake-up alarms** switch the room light on with a sunrise fade, set per
weekday like the sleep schedule on the iPhone.
An optional **presence check** listens for your phone over Bluetooth shortly before an alarm:
if it isn't there, the alarm doesn't ring.
**Daylight switching** turns the room light on when it gets dark and off when it gets bright,
from the weather forecast of open-meteo.com.

## Requirements

- ESP32-S3 DevKit (board **ESP32S3 Dev Module**). The onboard RGB LED (GPIO 48) is switched off on boot.
- [Arduino IDE 2](https://www.arduino.cc/en/software) or `arduino-cli`
- Board package **esp32** by Espressif, 3.x (built with 3.3.12)
- Library **NimBLE-Arduino** 2.x (built with 2.5.1)

## Setup

1. Copy `lights/secrets.example.h` to `lights/secrets.h` and enter your Wi-Fi name and password,
   the mesh name and password of the room light and your location for the weather forecast.
   `secrets.h` is in `.gitignore`, so your passwords and location never get committed.
2. Check `lights/config.h`, especially the BLE addresses of both lights.
3. Open `lights/lights.ino` in the Arduino IDE, select **ESP32S3 Dev Module** and the right port, and upload.
4. Open the serial monitor at 115200 baud. Once Wi-Fi is connected it prints the IP address.
5. Open `http://<ip>/` in a browser: on and off for both lights, with links to `/led`, `/room`
   and `/alarm`. The presence page at `/presence` and the daylight routes under `/daylight` aren't
   linked anywhere; open them directly.
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
├── home_page.*          start page at /: on and off for both lights
├── light_page.*         HTML control page shared by all lights
├── serial_console.*     single-character test commands over the serial monitor
├── led_lamp.*           LED strip driver: BLE connection, commands, status (namespace LedLamp)
├── led_http.*           HTTP routes under /led
├── room_light.*         room light driver: BLE connection, pairing, encryption (namespace RoomLight)
├── room_light_http.*    HTTP routes under /room
├── alarm_clock.*        alarms: time sync, schedules saved in flash, sunrise (namespace AlarmClock)
├── alarm_http.*         HTTP routes under /alarm
├── alarm_page.*         the alarm page
├── presence.*           is my phone here? BLE scan on demand, pairing (namespace Presence)
├── presence_http.*      HTTP routes under /presence
├── presence_page.*      the presence page
├── weather.*            downloads the forecast from open-meteo.com (namespace Weather)
├── daylight.*           room light by the sun and the weather, with a log (namespace Daylight)
└── daylight_http.*      HTTP routes under /daylight
```

The rule of thumb: each light has a **driver** (talks to the device) and an **HTTP file**
(turns requests into driver calls). Shared things like Wi-Fi and the web server know nothing
about any specific light.

All files sit directly in the sketch folder because the Arduino IDE only shows (and compiles
without extra include paths) files at the top level of the sketch.

## HTTP API

The start page at `/` has on and off buttons for both lights and links to their pages and the
alarms. It uses the routes below, nothing of its own.

Both lights work the same way:

- Every endpoint accepts **GET or POST**.
- Parameters go in the query string (`?r=255&g=80&b=0`) or a JSON body (`{"r":255,"g":80,"b":0}`).
- Answers are JSON with `"ok":true`, errors are `{"ok":false,"error":"..."}`
  (400 bad parameter, 404 unknown path, 503 lamp not connected).
- `/toggle` switches the light off if it is on, and on otherwise. If the lamp isn't connected or
  its state is unknown, it switches it on. It answers like `/on` and `/off` (`"power":"on"` or
  `"power":"off"`).
- **`if_dark=1`** on `/on`, `/toggle`, `/rgb` and `/white` switches only while it's dark, by the
  same rules as [daylight switching](#daylight--daylight): from 45 min before sunset until
  sunrise, or while the solar radiation is below 50 W/m². While it's bright, the light stays as it
  is and the answer is `{"ok":true,"switched":false,"dark":false}`; `/toggle` still switches off.
  Without a forecast for now it counts as dark. The forecast is only downloaded while daylight
  switching is on, and lasts 24 h. `"now"` in `/daylight/status` shows what `if_dark` sees.

### Room light – `/room`

| Path           | Parameters                                                           |
| -------------- | -------------------------------------------------------------------- |
| `/room`        | web control page (GET)                                               |
| `/room/on`     | –                                                                    |
| `/room/off`    | –                                                                    |
| `/room/toggle` | –                                                                    |
| `/room/rgb`    | `r`, `g`, `b` (0–255) and/or `brightness` (1–100)                    |
| `/room/white`  | `temperature` (0 = cold … 100 = warm) and/or `brightness` (1–100); neither = 50 / 70 |
| `/room/status` | –                                                                    |

```bash
curl "http://<ip>/room/rgb?r=255&g=80&b=0&brightness=80"
```

`/room/toggle` reads the lamp's status like `/room/status` and switches the way `/room/on` and
`/room/off` do.

`/room/status` reads a fresh status from the lamp. The values are the lamp's raw ones
(`white_brightness` / `white_temp` 0–127, `color_brightness` 10–100, `mode` 1 = white,
3 = color), `null` while unknown:

```json
{"ok":true,"connected":true,"state":{"status":1,"is_on":true,"mode":3,"white_brightness":89,"white_temp":64,"color_brightness":80,"red":255,"green":80,"blue":0}}
```

### LED strip – `/led`

| Path          | Parameters                                                          |
| ------------- | ------------------------------------------------------------------- |
| `/led`        | web control page (GET)                                              |
| `/led/on`     | –                                                                   |
| `/led/off`    | –                                                                   |
| `/led/toggle` | –                                                                   |
| `/led/rgb`    | `r`, `g`, `b` (0–255), optional `brightness` (1–100, scales the color) |
| `/led/white`  | optional `brightness` (1–100, default 70)                           |
| `/led/status` | –                                                                   |

`/led/toggle` asks the strip whether it is on and waits up to `LED_LAMP_STATUS_TIMEOUT_MS` (1 s)
for the answer. Without one, it switches the strip on.

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
- **Only when dark** (`dark_only`, off by default): the schedule's alarm stays off when it's
  bright outside at its time, by the same rules as `if_dark` (see [HTTP API](#http-api)): the sun
  is up and the radiation is above 50 W/m². Handy in summer, when the sun wakes you anyway. The
  ESP asks the forecast once, just before the sunrise would start; without a forecast for then
  it rings. Until then the page shows **Too bright** on the next alarm when the forecast says so
  (`too_bright` in `upcoming`). The forecast needs daylight switching on (`/daylight/on`).
  One-time alarms always ring.
- **Next alarm only:** one scheduled alarm can have a different time or be skipped, for that
  day only. The change is dropped once the day is over. A changed time keeps the schedule's
  *only when dark*.
- **One-time alarm:** rings once at the next time it is `hour:minute` (today if still ahead,
  otherwise tomorrow), apart from the schedule. It switches the light on at once, without a
  sunrise, at the default brightness and temperature (`DEFAULT_*` in `alarm_clock.h`).
- **Snooze:** during a sunrise and up to `ALARM_SNOOZE_OFFER_S` (1 h) after an alarm, the page
  offers 5 / 10 / 20 min. The light goes off right away and comes back on at once after that,
  with the brightness and temperature of the alarm that rang, and its *only when dark*. An alarm
  due before the snooze ends doesn't ring; the snooze takes it over. The iPhone's snooze button
  can do the same, see
  [Snooze with the iPhone alarm](#snooze-with-the-iphone-alarm).
- If the room light is already on when the sunrise would start, the alarm leaves it alone.
  Switching the room light off or setting a color or white (`/room/off`, `/room/toggle`, `/rgb`,
  `/white`) ends a running sunrise.

| Path                | Parameters                                                                 |
| ------------------- | -------------------------------------------------------------------------- |
| `/alarm`            | alarm page (GET)                                                           |
| `/alarm/status`     | –                                                                          |
| `/alarm/on`         | – (master switch: alarms ring)                                             |
| `/alarm/off`        | – (no alarm rings, schedules are kept)                                     |
| `/alarm/save`       | `id` (missing = new schedule), `days`, `hour`, `minute`, `fade` (0–60), `brightness` (1–100), `temperature` (0–100), `enabled` (0/1), `dark_only` (0/1) |
| `/alarm/delete`     | `id`                                                                       |
| `/alarm/next`       | `hour` and `minute`, or `skip=1`; optional `date` (`yyyymmdd`, default: the next scheduled alarm) |
| `/alarm/next/reset` | – (back to the schedule)                                                   |
| `/alarm/once`       | `hour`, `minute` (one-time alarm; replaces the one set before)             |
| `/alarm/once/remove`| –                                                                          |
| `/alarm/snooze`     | `minutes` (1–120, default 10); switches the light off and answers with `"snoozed":true` and `"light_off"`. `if_alarm=1`: only while an alarm is going on (see below); `if_dark=1`: only while it's dark (see [HTTP API](#http-api)). Otherwise `"snoozed":false` |
| `/alarm/snooze/cancel` | –                                                                       |
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
 "schedules":[{"id":0,"days":62,"time":"06:45","enabled":true,"fade":20,"brightness":70,"temperature":50,
               "dark_only":false}],
 "upcoming":[{"kind":"schedule","date":20261019,"weekday":1,"time":"06:45","schedule":0,"changed":false,
              "skipped":false,"fade":20,"brightness":70,"temperature":50,"dark_only":false,
              "too_bright":false,"in_minutes":512}],
 "sunrise":null,"one_time":null,"snooze":null,"rang":null}
```

`upcoming` holds the next alarm and the one after it (useful when the next one is skipped).
`kind` is `schedule`, `one_time` or `snooze`. `one_time` and `snooze` show those alarms even
when they are not among the next two. `sunrise` is set while one is running:
`{"time":"06:45","progress":40,"lamp_connected":true}`. `rang` is set for an hour after an alarm
(unless snoozed), for the snooze buttons: `{"time":"05:55","minutes_ago":2}`.

#### Snooze with the iPhone alarm

When you snooze the iPhone's alarm, the light goes off and comes back on together with it. An
iPhone automation can't be tied to snoozing the Sleep focus wake-up alarm, only to snoozing *any*
alarm. So the automation calls `/alarm/snooze?minutes=9&if_alarm=1` for every snoozed alarm, and
the ESP decides. It snoozes only while one of its alarms is going on:

- a sunrise is running,
- an alarm or snooze rang up to `ALARM_PHONE_SNOOZE_S` (20 min) ago,
- a snooze is set, or
- the next alarm is due before the snooze ends (it is moved to the snooze time).

If that alarm's schedule is *only when dark*, it also has to be dark, as with `if_dark=1`. So
`if_alarm=1` alone already follows the schedule's setting. Otherwise (a timer alarm at 3 pm, the
alarms are off) it answers `"snoozed":false` and nothing changes. The light comes back on `ALARM_SNOOZE_EARLY_S` (15 s) before the snooze is up, after
8:45 for 9 minutes, so it's on when the phone rings again.

Give the ESP a fixed IP in your router (DHCP reservation), then on the iPhone:

1. **Shortcuts** → **Automation** → **+** → **Alarm**.
2. Pick **Is Snoozed** for any alarm, and **Run Immediately** (turn off **Notify When Run**).
   **Next** → **New Blank Automation**.
3. Add **Get Contents of URL** with `http://<ip>/alarm/snooze?minutes=9&if_alarm=1`.
   `minutes` must match the phone's snooze length, which is 9 by default.
4. Tap ▶ once to test. iOS asks for access to the local network; allow it. Unless an alarm
   is going on, the answer ends with `"snoozed":false` and the light stays as it is.

Optional:

- **Only in the dark, for every schedule:** add `&if_dark=1` to the URL, so a bright morning
  doesn't switch the light off and back on. For single schedules, use *only when dark* instead.
- **Away from home** the request fails and the iPhone shows an error. To avoid that, put
  **Get Network Details** (Wi-Fi, Network Name) first and the URL inside **If** *Network Details*
  **is** *your Wi-Fi name*.
- **Stopping the alarm** while it's snoozed: a second automation, **Is Stopped** for any alarm,
  with `http://<ip>/alarm/snooze/cancel` keeps the light from coming back on. When no snooze is
  set, it changes nothing.

### Presence – `/presence`

Not linked from the other pages; open `http://<ip>/presence` directly.

Is your phone here? The ESP listens for the Bluetooth advertisements every phone sends, but
only when asked: `PRESENCE_CHECK_LEAD_S` (2 min) before each alarm, before daylight switches the
room light on, and when you tap **Check now** on the page. A check listens for at most
`PRESENCE_SCAN_S` (30 s) and stops as soon as the phone is heard. Nothing runs in between. If the
phone isn't heard, the alarm doesn't ring and daylight leaves the light off. While presence is off,
or no phone is paired, both work as usual.

**Pairing (once).** iPhones change their Bluetooth address about every 15 minutes, so the ESP
needs the phone's identity key (IRK) to recognize it. It gets the key by pairing:

1. On `/presence`, tap **Pair phone**. For 2 minutes the ESP can be found as `BLE_DEVICE_NAME`
   (`ESP32-Lamp-Test`).
2. On the iPhone, open a Bluetooth app like **nRF Connect** or **LightBlue** (both free) and
   connect to it. The iPhone's Bluetooth settings don't list devices like this one, so an app is
   needed for this one step.
3. Tap **Pair** when the iPhone asks. If it doesn't ask, open the service in the app and read its
   characteristic.

The key is saved in flash. If pairing fails after **Forget** on the page, also forget the ESP in the
iPhone's Bluetooth settings and pair again.

**Signal strength.** The page shows the signal (RSSI, in dBm) of the last check. **Watch signal**
listens for `PRESENCE_WATCH_S` (1 min) and shows every reading, so you can walk around and see how
it changes. By default anything the ESP can hear counts as "here", which can be the whole flat.
To count only your room, set `PRESENCE_MIN_RSSI` in `config.h` to a value between what you see
in bed and in the next room (e.g. -75).

**Load.** Only while a check runs: the scan listens 30 ms of every 100 ms, so Wi-Fi and the lamp
connections keep most of the radio. It costs the phone nothing, since it advertises anyway. A
pairing service with one characteristic sits in memory all the time, but it's only visible
while pairing.

| Path                     | Parameters                                                        |
| ------------------------ | ----------------------------------------------------------------- |
| `/presence`              | presence page (GET)                                               |
| `/presence/status`       | –                                                                 |
| `/presence/on`           | – (master switch, saved in flash)                                 |
| `/presence/off`          | – (no scanning or advertising at all, alarms ring as usual)       |
| `/presence/check`        | optional `watch=1` (listen 1 min and show every reading)          |
| `/presence/stop`         | – (ends a running check)                                          |
| `/presence/pair`         | – (ready to pair for `PRESENCE_PAIR_WINDOW_S`)                    |
| `/presence/pair/cancel`  | –                                                                 |
| `/presence/forget`       | – (forgets the paired phone)                                      |

Every route answers with the full status:

```json
{"ok":true,"enabled":true,"phone":true,"answer":"present","pairing":null,"pair_ok":false,
 "check":{"running":false,"reason":"manual","seen":true,"found":true,"rssi":-64,"best_rssi":-61,
          "heard_ago_s":42,"ago_s":40,"left_s":0},
 "device_name":"ESP32-Lamp-Test","min_rssi":-100,"scan_s":30,"lead_s":120}
```

`answer` is `present`, `away`, `checking` or `unknown` (off, no phone, or no check in the last 10
minutes). `check` is the running or last check (`null` before the first one); `reason` is
`manual`, `alarm`, `daylight` or `watch`. `found` means heard with at least `min_rssi`. `pairing` is the seconds
left while the ESP can be paired, and `pair_ok` tells whether the last pairing worked.

### Daylight – `/daylight`

Not linked from the other pages, and there's no page: the routes answer in JSON, which the browser
shows as it is.

The room light by daylight, switched by the sun and by the weather. The ESP downloads the forecast
for your location (`WEATHER_LATITUDE` / `WEATHER_LONGITUDE` in `secrets.h`) from open-meteo.com:
the solar radiation every 15 minutes for the next 24 hours, and sunrise and sunset.

- **By the sun:** on `DAYLIGHT_SUNSET_LEAD_S` (45 min) before sunset, off at sunrise.
- **By the weather:** on when the radiation drops below `DAYLIGHT_RADIATION_THRESHOLD` (50 W/m²),
  off when it rises above it again. The time is taken on a straight line between the 15-minute
  values. After switching, the weather waits an hour before it switches again, so passing clouds
  don't make the light go on and off.
- The light only switches when the sun or the weather **changes**, so you can switch it yourself in
  between. Each of them switches on its own: on a cloudy evening the weather switches the light
  on, and 45 min before sunset the sun finds it on already.
- **On** fades in over `DAYLIGHT_FADE_S` (5 min), from the lowest brightness and warmest white to
  `DAYLIGHT_BRIGHTNESS` and `DAYLIGHT_TEMPERATURE`, like a short alarm sunrise. Before that, the
  presence check looks for your phone: if it isn't here, the light stays off. With presence off or
  no phone paired, it comes on as usual. A light that is on already is left alone.
- **Off** doesn't ask for the phone.
- While an alarm's sunrise runs, daylight leaves the light to the alarm. Switching the room light
  off or setting a color or white (`/room/off`, `/room/toggle`, `/rgb`, `/white`) ends a running
  fade.
- If the lamp is unreachable, it keeps trying for `DAYLIGHT_RETRY_S` (10 min). A change while the
  ESP was off still switches when the ESP is back within those 10 min; older ones don't, like the
  alarms. So a reboot at night doesn't switch the light on.
- The forecast is downloaded every 15 min while the weather is about to switch the light (within
  3 h), every 3 h while that is up to 9 h away, and every 6 h otherwise. After an error it tries
  again in 15 min and keeps using the last forecast meanwhile.

| Path                | Parameters                                                 |
| ------------------- | ---------------------------------------------------------- |
| `/daylight/status`  | –                                                          |
| `/daylight/log`     | – (every switch, newest first)                             |
| `/daylight/on`      | – (master switch, saved in flash)                          |
| `/daylight/off`     | – (nothing is downloaded or switched)                      |
| `/daylight/refresh` | – (downloads the forecast now)                             |

**The log** keeps the last 40 switches in flash, so it survives a reboot:

```json
{"ok":true,"log":[
 {"time":"2026-10-22 07:53:17","trigger":"sunrise","action":"off","result":"off","brightness":70,"temperature":50,"radiation":3},
 {"time":"2026-10-21 17:40:42","trigger":"sunset","action":"on","result":"on","brightness":70,"temperature":50,"radiation":12}]}
```

`trigger` is `sunset`, `sunrise`, `dark` or `bright` (the radiation crossed the threshold), and
`action` follows from it. `result`:

- `on` – switched on; `brightness` and `temperature` are what it fades to (percent)
- `off` – switched off; `brightness` and `temperature` are what the light had (`temperature` is
  `null` in color mode)
- `already_on`, `already_off` – left as it was
- `phone_away` – the presence check didn't hear your phone, so the light stayed off
- `lamp_unreachable` – the lamp wasn't reachable for `DAYLIGHT_RETRY_S`
- `alarm_running` – an alarm's sunrise had the light

`radiation` is the forecast's W/m² at that moment.

**The status** shows what the ESP plans to do:

```json
{"ok":true,"enabled":true,"time_valid":true,"radiation":28.8,"sun":"bright","weather":"dark","now":"dark",
 "next_sun":{"time":"2026-10-21 17:40:42","action":"on"},
 "next_weather":{"time":"2026-10-22 08:30:55","action":"off"},"switching":null,
 "forecast":{"downloaded":"2026-10-21 17:23:29","until":"2026-10-22 17:00:00","error":null,"next_download_min":360},
 "settings":{"threshold":50,"sunset_lead_min":45,"fade_s":300,"brightness":70,"temperature":50}}
```

`sun` and `weather` are what each said last (`dark` = light on, `null` until there is a forecast).
`now` is what `if_dark=1` sees: `dark` when the sun or the weather says so right now (without the
weather's one-hour wait), `null` without a forecast for now.
`radiation` is the forecast's value for now. `switching` is set while a switch runs:
`{"action":"on","trigger":"sunset","step":"fade","progress":40}`, where `step` is `presence`
(looking for the phone), `lamp` (connecting) or `fade`.

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

`A` prints the ESP's time and the next alarm. `P` looks for the phone; the result is printed
when the check ends.

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
- While the ESP is connected to the room light, other devices like the AwoX app can't connect to it.
- Alarms need the time from the internet (NTP). Until the ESP has it after a boot, no alarm rings and
  the alarm page says so. Alarms that were due while the ESP was off don't ring later.
- Before a sunrise the ESP starts connecting to the room light `ALARM_PRECONNECT_S` early. If the
  lamp is still unreachable, it keeps trying until `ALARM_RETRY_S` after the alarm time.
- With presence on, the room light only gets connected once the phone check is done (at most 30 s).
  A lamp reconnecting during a check pauses the scan; the check keeps going until its time is up.
- The presence check only knows whether your phone is near, not whether you are. A phone left at
  home counts as you being home, and with Bluetooth turned off in Settings the phone isn't found.
- Daylight needs the internet for the forecast. Without it, the last forecast lasts 24 hours;
  after that the light isn't switched until a download works again.
- Downloading the forecast blocks the ESP for a moment (about 0.1–0.5 s, at most ~10 s when
  open-meteo.com is unreachable). It uses plain HTTP, which saves the ~40 KB of memory TLS would
  need; the data is public, but someone on the network path could fake it.
