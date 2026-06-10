# BatteryKit_BringUp

Per-feature hardware verification for the **Battery Kit V0.3**. No MistMaker library required — flash this first on a freshly assembled board.

**Dependencies:** none. **Board:** XIAO ESP32-C6 (`XIAO_ESP32C6` in Tools > Board).

## Checklist (serial @ 115200)

| Do | Verifies |
|---|---|
| press button | Button wiring — mist + LED toggle |
| `t` | Boost rail (D3) + PWM (D0) — disc in water should mist |
| `c` | INA180 current reading on D2 |
| `b` | Battery divider on D1 — unplug USB for a true pack reading |
| `s` | CSV stream (mA + Vbatt) for the Serial Plotter |
| `0`–`9` | Duty 0–90% dimming |
| `h` | Help |

Expected at duty 64: no disc ≈ 0 mA · dry disc ≈ 70–100 mA · disc in water ≈ 130–200 mA.
Battery: 4.2 V full · <3.45 V low · <3.20 V critical.

> Remember: connect USB **before** the battery during development or the serial port may not enumerate.

Pass everything? Move on to the [MistMaker library](https://github.com/owochel/MistMaker) examples — `WiFiPhoneControl` includes the graceful low-battery shutdown.
