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
| unplug USB, replug, `u` | **Mux handoff** — flip counter + boot reason prove the USB↔cell switchover didn't reset the MCU |
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

### The handoff test — live unplug/replug (V0.4)

The TPS2116 switches sources **break-before-make**: for ~1.3 ms neither input drives the rail, and the board must ride through on its capacitors (the 3.3 V handoff also involves Q1 re-enabling the onboard LDO). The sketch counts every USB↔cell flip in RAM and prints the **reset reason** at boot, so the test works even though serial dies while unplugged:

1. Cell plugged in, mist running (`t`), USB connected.
2. Unplug USB — mist should keep running from the cell. Wait a few seconds.
3. Replug USB, reopen serial, press `u`.
4. **Pass:** handoff count ≥ 2 and boot reset still reads `power-on` (a fresh `BROWNOUT` line means the gap dipped too deep — check C6/C12/C15 soldering).

### Power notes (as-built V0.4, June 2026 run)

- The ~5 V boost rail is **live from power-on** (R8 pulls the TPS61023 EN high) until the sketch drives D3 low in `setup()`. The piezo stays off regardless (R11 holds the MOSFET gate down) — don't be surprised by 5 V on a scope before boot.
- Charge current is **~196 mA** (R6 = 5.1 kΩ on the LP4060) — a deliberate thermal derate from the 500 mA design. LED1 (green) lit = charging; a 500 mAh cell takes ≈ 3 h from empty.
- The battery divider (R18/R19) is hard-wired and draws a constant **~210 µA** from the cell — negligible in use, but don't expect shelf-storage battery life with a cell left plugged in.

Pass everything? Move on to the [MistMaker library](https://github.com/owochel/MistMaker) (≥ 1.2.0) examples — `WiFiPhoneControl` re-enables graceful low-battery shutdown, now gated on the D8 mux status.
