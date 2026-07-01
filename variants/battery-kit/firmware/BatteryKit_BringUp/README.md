# BatteryKit_BringUp

Per-feature hardware verification for the **Battery Kit V0.4** (pins D0–D7 are identical on V0.3; the `u` USB/mux test needs V0.4). No MistMaker library required — flash this first on a freshly assembled board.

**Dependencies:** none. **Board:** XIAO ESP32-C6 (`XIAO_ESP32C6` in Tools > Board).

## Checklist (serial @ 115200)

| Do | Verifies |
|---|---|
| press button | Button wiring — mist + LED toggle |
| `t` | Boost rail (D3) + PWM (D0) — disc in water should mist |
| `c` | INA180 current reading on D2 |
| `u` | Power-mux status on D8 (V0.4) — plug/unplug USB, the source flips |
| `b` | Battery divider on D1 — auto-tagged valid only when on the cell |
| `s` | CSV stream (mA + Vbatt + USB) for the Serial Plotter |
| `0`–`9` | Duty 0–90% dimming |
| `h` | Help |

Expected at duty 64: no disc ≈ 0 mA · dry disc ≈ 70–100 mA · disc in water ≈ 130–200 mA.
Battery: 4.2 V full · <3.45 V low · <3.20 V critical.

### The `u` test — power mux status (V0.4)

D8 is the TPS2116 **ST** (status) pin, divided down through R21 (100K) / R22 (150K):

| State | D8 | `u` / `b` report |
|---|---|---|
| USB plugged in | **HIGH** (~2.6 V) | `present : mux on VIN1 (USB)` — Vbatt is *charging*, not a state-of-charge |
| running on cell | **LOW** (~0 V) | `absent : mux on VIN2 (battery)` — Vbatt is a **valid** state-of-charge |

Because the mux runs the board off USB whenever it's plugged in, the battery voltage only means "state of charge" when D8 is LOW. `b` and the `[STAT]` line use D8 to tag the reading automatically — this is the fix for the V0.3 false low-battery shutdown.

> D8 must be a plain `INPUT` — the R21/R22 divider sets the level, so an internal pull would flip the reading. On V0.3 D8 is an unconnected spare, so `u` is meaningless there.

Pass everything? Move on to the [MistMaker library](https://github.com/owochel/MistMaker) (≥ 1.2.0) examples — `WiFiPhoneControl` re-enables graceful low-battery shutdown, now gated on the D8 mux status.
