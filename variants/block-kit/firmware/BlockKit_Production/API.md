# Block Kit Mist Maker — HTTP API

Reference for building a companion device (ESP32-S3, Pi, Mac script, anything on the LAN) that controls or reacts to the Block Kit Mist Maker.

The firmware exposes a small HTTP/JSON API plus a Server-Sent Events stream. No SDK needed — `curl`, Arduino `HTTPClient`, Python `requests`, all work.

## 1. Discovery and connection

| Property         | Value                                                                  |
| ---------------- | ---------------------------------------------------------------------- |
| Hostname (mDNS)  | `mistmaker.local` (configurable via `/api/config` `hostname` field)     |
| Port             | `80` (HTTP)                                                            |
| Protocol         | HTTP/1.1, plain text (no TLS)                                          |
| Discovery        | mDNS `_http._tcp.local`; fallback: scan LAN or use static IP from router |
| Captive portal   | If the device is unprovisioned it opens an AP `BlockKit-Setup-<MAC>`     |

Curl example:

```bash
curl http://mistmaker.local/api/status | jq
```

If mDNS doesn't resolve from your platform (some Linux without Avahi), use the IP shown in `/api/info` or your router's DHCP table.

## 2. Authentication

- Read endpoints (GET) and the SSE stream are **public** on the LAN.
- Write endpoints (POST) require **HTTP Basic auth** with username `admin` and the admin password set during WiFi setup.
- First-boot grace: until an admin password is set, writes are open. As soon as a password is set (in the web UI or via `POST /api/cmd/password`), writes lock.
- The factory-reset endpoint (`POST /api/cmd/factory-reset`) always requires an admin password — there is no first-boot grace.

Curl with auth:

```bash
curl -u admin:<password> -X POST http://mistmaker.local/api/cmd/level \
     -H 'Content-Type: application/json' -d '{"value":180}'
```

Arduino `HTTPClient` with auth:

```cpp
HTTPClient http;
http.begin("http://mistmaker.local/api/cmd/level");
http.setAuthorization("admin", adminPassword);
http.addHeader("Content-Type", "application/json");
int rc = http.POST("{\"value\":180}");
```

## 3. Endpoint reference

| Method | Path                          | Auth        | Purpose                                                         |
| ------ | ----------------------------- | ----------- | --------------------------------------------------------------- |
| GET    | `/`                           | none        | Web UI (HTML)                                                   |
| GET    | `/api/status`                 | none        | Live status snapshot (see §4)                                   |
| GET    | `/api/config`                 | none        | Tunable config snapshot (no secrets)                            |
| POST   | `/api/config`                 | admin       | Apply + save one config field. Body: `{"field":"name","value":N}` |
| GET    | `/api/info`                   | none        | Static device info (mac/ip/firmware/hostname)                   |
| GET    | `/api/log`                    | none        | Last ~6 KB of serial log as plain text                          |
| GET    | `/api/events`                 | none        | Server-Sent Events stream of `/api/status` (§5)                 |
| POST   | `/api/cmd/level`              | admin       | Set user level 0..255. Body: `{"value":N}` (mist + LED intensity) |
| POST   | `/api/cmd/state`              | admin       | Force state. Body: `{"state":"idle"\|"running"}`                |
| POST   | `/api/cmd/leds`               | admin       | Toggle LED strip hide/show (mist unaffected). No body.          |
| POST   | `/api/cmd/walk`               | admin       | Run a one-shot LED chase animation. No body.                    |
| POST   | `/api/cmd/statled`            | admin       | Override indicator LED. Body: `{"mode":"auto"\|"on"\|"off"}`    |
| POST   | `/api/cmd/scope`              | admin       | Toggle current-sense scope-mode CSV stream on Serial. No body.  |
| POST   | `/api/cmd/plotmute`           | admin       | Toggle `[PLOT]` serial log mute. No body.                       |
| POST   | `/api/cmd/calibrate-water`    | admin       | Run water-level probe; returns recorded mA + recommended low threshold. Only valid in RUNNING with mist flowing — else `409`. |
| POST   | `/api/cmd/reboot`             | admin       | Restart in 250 ms. No body.                                     |
| POST   | `/api/cmd/forget`             | admin       | Wipe WiFi credentials and reboot into captive portal. No body.  |
| POST   | `/api/cmd/factory-reset`      | admin (forced) | Wipe entire NVS (config + WiFi + admin password) + reboot. No body. |
| POST   | `/api/cmd/password`           | admin       | Change admin (+ OTA) password. Body: `{"new":"newpassword"}`. Min 4 chars. |

Common response shape: `{"ok":true}` on success, `{"error":"reason"}` on failure. Standard HTTP status codes (`200`, `400` bad input, `401` auth, `403` forbidden, `409` wrong state, `500` server error).

## 4. `/api/status` schema

JSON object emitted by `GET /api/status` and each SSE `data:` frame. Stable wire contract — companion devices can depend on these field names.

```json
{
  "state": "IDLE",            // string: "IDLE" | "RUNNING" | "XFADE_OUT"
  "stateInt": 0,              // int: 0 IDLE / 1 RUNNING / 2 XFADE_OUT
  "btnRaw": 1,                // int: raw digitalRead of the physical button (1=released)
  "reedRaw": 0,               // int: raw debounced reed (1=magnet detected)
  "reedPresent": 0,           // int: 1 if the firmware considers a container docked
  "mist": 0,                  // int: 1 if the piezo boost rail is currently energized
  "ledsHidden": 0,            // int: 1 if the LED strip has been faded out by short-press
  "setupMode": 0,             // int: 1 if device is in WiFi captive-portal setup mode
  "userLevel": 255,           // u8: 0..255 user-set intensity (mist + LED)
  "currentLevel": 255,        // u8: smoothed level actually being applied
  "meanMa": 12.3,             // float: rolling 256-sample mean current (mA)
  "varMa2": 0.5,              // float: rolling variance, mA^2
  "piezoState": "WATER_OK",   // string: "UNKNOWN" | "DISC_MISSING" | "DISC_DRY" |
                              //         "WATER_OK" | "WATER_LOW" | "WATER_DEPLETED" |
                              //         "DISC_DISCONNECTED"
  "piezoProbeMa": 128.4,      // float: mA from the most recent classifier probe
  "waterCountdownS": 0,       // u32: seconds remaining in WATER_LOW countdown (0 = idle)
  "uptimeMs": 12345678,       // u32: millis() since boot
  "freeHeap": 162400,         // u32: bytes of free heap
  "rssi": -52,                // int: WiFi RSSI in dBm
  "statLedOverride": -1       // int: -1 auto / 0 force-off / 1 force-on
}
```

`piezoState` is set by the current-sense classifier and is the field a companion device should subscribe to for "the user's session is starting / running / running-out / done." See `piezo_sense.ino` for the threshold logic and the `senseUseAsReed` / threshold tunables in `/api/config`.

## 5. Server-Sent Events stream

`GET /api/events` opens a long-lived HTTP connection that emits one `data:` line every 250 ms containing the same JSON as `/api/status`. Single concurrent subscriber — a new GET replaces the previous client.

Plain `curl`:

```bash
curl -N http://mistmaker.local/api/events
# data: {"state":"IDLE",...}
# data: {"state":"IDLE",...}
```

Arduino `WiFiClient` consumer (sketch fragment for an ESP32-S3 companion):

```cpp
WiFiClient client;
if (client.connect("mistmaker.local", 80)) {
  client.print("GET /api/events HTTP/1.1\r\nHost: mistmaker.local\r\nConnection: keep-alive\r\nAccept: text/event-stream\r\n\r\n");
  // ...read lines forever, parse `data: ...` lines as JSON
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line.startsWith("data: ")) {
      String json = line.substring(6);
      // hand to ArduinoJson or hand-roll a few key extractions
    }
  }
}
```

## 6. Tunable config (`/api/config`)

`GET /api/config` returns the full config snapshot (no secrets). Each field can be individually updated via `POST /api/config` with `{"field":"NAME","value":N}`. Fields are validated and saved to NVS atomically.

Key fields a companion device might care about:

| Field                      | Range       | Meaning                                                                |
| -------------------------- | ----------- | ---------------------------------------------------------------------- |
| `levelDefault`             | 0..170      | Boot/default intensity                                                 |
| `mistDutyMax`              | 0..170      | PWM duty for "high" (cap is 170 for safety; piezo OCP above)            |
| `mistDutyMin`              | 0..170      | PWM floor while level > 0                                              |
| `mistWaveTroughQ8`         | 0..256      | Mist-wave depth (Q8 fixed-point: 256 = no swell, 0 = full swing)        |
| `senseProbeDuty`           | 0..255      | PWM used for disc-presence probe (default 10)                          |
| `senseDiscPresentMa10x`    | 0..5000     | Threshold above which a disc is "present" at `senseProbeDuty` (mA × 10) |
| `senseWaterProbeDuty`      | 0..255      | PWM used for water-level probe (default 64)                            |
| `senseWaterLowMa10x`       | 0..5000     | Threshold below which water is "low" at `senseWaterProbeDuty` (mA × 10) |
| `senseWaterCheckIntervalS` | 5..3600     | Seconds between periodic water-level probes (default 60)                |
| `senseWaterShutdownS`      | 30..7200    | Seconds of WATER_LOW before WATER_DEPLETED hard-stop (default 300 = 5 min) |
| `senseUseAsReed`           | 0 or 1      | If 1, current-sense replaces the reed switch for dock detection         |
| `senseAutoProbeIntervalS`  | 1..3600     | Seconds between auto-probes in IDLE when `senseUseAsReed=1` (default 5) |

Thresholds use the `mA × 10` convention to keep one decimal place of precision in NVS without floating-point.

## 7. Worked example — breathing-correlated mist controller

Scenario: an ESP32-S3 device with a presence/breathing sensor wants to ramp mist intensity with the user's breathing rate, and stop the diffuser when no presence is detected.

Loop (pseudocode — full Arduino sketch left as an exercise):

```cpp
// every breathing-detector frame (say 1 Hz)
const int breathsPerMin = breathDetector.bpm();
const bool presence     = breathDetector.presence();

if (!presence) {
  // No one around — pause the mist but stay in RUNNING so a re-detect resumes fast.
  postJson("/api/cmd/level", "{\"value\":0}");
  return;
}

// Map breathing rate to a soothing mist level. Calmer breathing -> gentler mist.
// 12 bpm (deep relaxed) -> level 80; 20 bpm (alert) -> level 180.
const int level = constrain(map(breathsPerMin, 12, 20, 80, 180), 0, 170);
postJson("/api/cmd/level", "{\"value\":" + String(level) + "}");
```

To respond to mist-side faults (out of water etc.), subscribe to `/api/events` and check `piezoState`:

```cpp
// when an SSE frame arrives:
if (status.piezoState == "WATER_LOW") {
  // surface a "refill soon" indicator on your companion display
}
if (status.piezoState == "WATER_DEPLETED" || status.piezoState == "DISC_DISCONNECTED") {
  // device has stopped misting; user must lift + redock
}
```

For tightly-coupled experiments you can even rewrite thresholds on the fly:

```cpp
// Make the diffuser more sensitive to early water-low warnings:
postJson("/api/config", "{\"field\":\"senseWaterLowMa10x\",\"value\":1200}");
```

## 8. Notes and gotchas

- Auth uses HTTP Basic. There is no token endpoint — store the admin password securely on the companion device or keep the device on a trusted network.
- The HTTP server is **synchronous**: only one in-flight request at a time. SSE is single-subscriber. If you need two consumers, have one device poll `/api/status` instead of subscribing to SSE.
- Probes block briefly (~30–150 ms). Don't issue a calibrate POST and expect a sub-millisecond response.
- `mistDutyMax` is firmware-capped at 170; values above that have been observed to trigger the boost converter's overcurrent hiccup, brown out the WiFi rail, and brick the device until a power-cycle or triple-tap reset.
- Factory reset wipes NVS — including WiFi credentials. Use deliberately.
