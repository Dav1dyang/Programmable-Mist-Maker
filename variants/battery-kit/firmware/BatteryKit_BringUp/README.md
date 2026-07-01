# BatteryKit_BringUp

Per-feature hardware verification for the **Battery Kit V0.4** (pins D0–D7 are identical on V0.3; the `u` USB/mux test needs V0.4). No MistMaker library required — flash this first on a freshly assembled board.

**Dependencies:** none. **Board:** XIAO ESP32-C6 (`XIAO_ESP32C6` in Tools > Board).

## Checklist (serial @ 115200)

| Do | Verifies |
|---|---|
| press button | Button wiring — mist + LED toggle |
| `t` | Boost rail (D3) + PWM (D0) — disc in water should mist |
| `c` | INA180 current reading on D2 |
| `u` | Power-mux status on D8 (V0.4) — reads USB present; meter TP3 for the cell case |
| `b` | Battery divider on D1 — auto-tagged valid only when on the cell |
| `s` | CSV stream (mA + Vbatt + USB) for the Serial Plotter |
| `0`–`9` | Duty 0–90% dimming |
| `h` | Help |

Expected at duty 64: no disc ≈ 0 mA · dry disc ≈ 70–100 mA · disc in water ≈ 130–200 mA.
Battery: 4.2 V full · <3.45 V low · <3.20 V critical.

### The `u` test — power mux status (V0.4)

D8 is the TPS2116 **ST** (status) pin, divided down through R21 (100K) / R22 (150K):

| State | D8 | Meaning |
|---|---|---|
| USB plugged in | **HIGH** (~2.6 V) | mux on VIN1 (USB) — Vbatt tracks the *charger*, not SoC |
| running on cell | **LOW** (~0 V) | mux on VIN2 (battery) — Vbatt is a **valid** state-of-charge |

`b` and `[STAT]` always print the voltage + OK/LOW/CRITICAL tag, and append `[USB - charging]` when D8 is HIGH so you know the number isn't a state-of-charge. This is the basis for the library's fix to the V0.3 false low-battery shutdown.

> **Serial caveat:** on this board USB *is* the serial link (native USB-Serial-JTAG), so over the console D8 essentially always reads HIGH. To confirm the LOW/cell state, meter **TP3** (or D8) with the cell in and USB out — you can't watch it flip live over serial.
>
> D8 must be a plain `INPUT` — the R21/R22 divider sets the level, so an internal pull would flip the reading. On V0.3 D8 is an unconnected spare, so `u` is meaningless there.

Pass everything? Move on to the [MistMaker library](https://github.com/owochel/MistMaker) (≥ 1.2.0) examples — `WiFiPhoneControl` re-enables graceful low-battery shutdown, now gated on the D8 mux status.
