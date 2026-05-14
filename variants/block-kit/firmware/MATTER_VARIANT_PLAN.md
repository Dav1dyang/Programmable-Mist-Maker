# Matter Variant — Plan (no code yet)

## Why a separate variant

The current `BlockKit_Production` sketch is at **95% of the default 1.3 MB app
partition**. Adding the Matter SDK pushes a typical minimal Matter example to
~1.6 MB on its own (per Espressif's ESP-Matter docs); stacked on top of our
existing WiFi/OTA/web UI it would not fit even on a 1.9 MB OTA partition.

A separate sketch (`BlockKit_Matter`) lets us:
- Pick a custom partition table sized for Matter (2 MB+ app, smaller SPIFFS).
- Drop features we don't need on the Matter path (the synchronous web UI is
  redundant once Apple Home / Google Home / Aqara own the control surface).
- Use ESP-IDF tooling where Arduino-on-top-of-IDF struggles (Matter's commissioning + Thread bring-up are easier in pure IDF).

The hardware is the same (XIAO ESP32-C6). Users pick which firmware to flash
based on what they want — fully local web UI (current production firmware)
**or** Matter-native pairing into a smart-home app (new variant).

## Target capabilities

The Matter spec calls our device a **Humidifier** (cluster `0x0072`). At v1.0
of the variant we'll implement the minimum surface:

- On/Off (cluster `0x0006`) — turn mist on/off.
- Level Control (cluster `0x0008`) — set mist intensity 0–100%.
- Humidifier (cluster `0x0072`) — current humidity mode.
- Identify (cluster `0x0003`) — blink LEDs when the smart-home app pings
  "identify this device" during commissioning.

Stretch (post-v1):
- Humidifier Concentration measurement (`0x0405`) reporting the desired
  humidity setpoint.
- Outdoor environmental data via thread border router → matter weather.

## QR code / commissioning

Matter uses a deterministic setup payload:
`MT:<Base38 of {VID,PID,discriminator,passcode,...}>`. Espressif's
`esp_matter` SDK generates this string at boot and provides:
1. A printable QR string (we'll print to Serial + show in initial setup AP).
2. A 32-char numeric setup code (the fallback when scanning fails).

The user scans the QR with the Apple Home / Google Home / Aqara app. The
device performs DNS-SD advertisement (`_matterc._udp` on WiFi, optionally
`_matter._tcp` on Thread once commissioned). After pairing the app owns
control — no more captive portal needed.

QR code RENDERING in the web UI is optional: ESP-Matter prints the payload;
we can render an SVG QR on a dedicated `/qr` route at first boot.

## Partition layout

For a 4 MB flash XIAO ESP32-C6, target layout:

```
# Name,        Type, SubType, Offset,   Size
nvs,           data, nvs,     0x9000,   0x6000
otadata,       data, ota,     0xf000,   0x2000
nvs_keys,      data, nvs_keys,0x11000,  0x1000   # for Matter DAC keystore
phy_init,      data, phy,     0x12000,  0x1000
app0,          app,  ota_0,   0x20000,  0x1F0000 # ~2 MB primary
app1,          app,  ota_1,   0x210000, 0x1E0000 # ~1.9 MB secondary
factory_mfg,   data, nvs,     0x3F0000, 0x6000   # Matter factory data
fctry,         data, nvs,     0x3F6000, 0x6000   # ESP-Matter factory namespace
matter_kvs,    data, nvs,     0x3FC000, 0x4000   # Matter runtime KVS
```

Total: 4 MB. Mature Matter + WiFi + OTA all fit; identical for ESP32-S3
hardware.

## Build approach

**Two options, decision deferred until prototyping:**

1. **arduino-esp32 v3.x + esp_matter library** — easier, all in Arduino IDE,
   but the Arduino Matter library lags ESP-IDF and may not expose every
   cluster we need.
2. **ESP-IDF + esp_matter** — full Espressif stack, requires `idf.py` build
   instead of Arduino. More setup pain, more capability.

We'd start with (1), fall back to (2) if blocked. Either path produces a
`.bin` flashable to the same XIAO board the current firmware runs on.

## What stays / what goes vs the current firmware

| Module | Matter variant |
|---|---|
| `pins.h` | reuse as-is |
| `mist.ino` | reuse — Matter cluster handlers call `mistApply()` |
| `led_driver.ino` | reuse — Matter Identify calls `ledWalk()` for blink |
| `container.ino` | reuse — drives state machine |
| `button.ino` | reuse — long-press = factory reset (Matter spec) |
| `current_sense.ino` | reuse for diagnostics |
| `BlockKit_Matter.ino` | NEW — state machine + Matter callbacks (replaces `BlockKit_Production.ino`) |
| `web_server.ino`, `web_ui.h` | **drop** — Matter app owns control |
| `wifi_setup.ino` | replace with Matter's commissioning flow |
| `ota.ino` | replace with Matter OTA Update Cluster |
| `config.ino`, `config.h` | trimmed — keep hardware-level NVS, lose web-UI fields |

## First milestone (proof of concept)

1. Clone the production firmware tree into `variants/block-kit/firmware/BlockKit_Matter/`.
2. Strip web UI + OTA + WiFiManager.
3. Drop in the partition table above.
4. Add `esp_matter` library, copy the Humidifier example.
5. Wire On/Off and Level Control cluster handlers to `mistEnable()` and
   `appSetLevel()`.
6. Print the QR setup payload on serial.
7. Commission with Apple Home — get to "mist on/off works from iPhone".

## What we'd want from the user before starting

- Choice of smart-home app to target first (Apple, Google, Aqara, Home
  Assistant, all of them).
- Whether OTA over Matter is required for v1, or USB flash is OK during
  prototyping.
- Decision on whether the standalone web UI (the current production firmware)
  should remain available as an alternate flash, or be deprecated entirely.

## Estimated effort

- Prototyping (Arduino + Matter library): 2–3 days.
- Polish + commissioning UX + factory-reset button: 1–2 days.
- Full Matter compliance + multi-app testing: 1 week.

A separate plan/PR opens that work; nothing in this firmware needs to change
to start that track.
