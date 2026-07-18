# BatteryKit_BringUp

Per-feature hardware verification for the **Battery Kit V0.4 / V0.4.1** (pins D0–D7 are identical on V0.3; the `u` USB/mux test needs V0.4+). No MistMaker library required — flash this first on a freshly assembled board.

**Dependencies:** none. **Board:** XIAO ESP32-C6 (`XIAO_ESP32C6` in Tools > Board).

## Automated self-test (start here)

**Hold the onboard button ≥ 1.5 s** (or send `a` over serial @ 115200) and the sketch runs the automatable slice of the V0.4 acceptance protocol end-to-end — about 10 seconds, disc in water recommended:

| Row | Measures | Pass window |
|---|---|---|
| `RESET` | Last reset reason | anything but `BROWNOUT` |
| `PWR_SRC` | D8 mux status, 40 reads must agree | stable HIGH (USB) or LOW (cell) |
| `VBAT` | Battery divider on D1 | 2.80–4.40 V pass · ≤ 0.5 V / > 4.40 V fail (board fault) · in between = info (discharged cell — charge & re-run) |
| `IDLE_MA` | INA180 zero, rail off | ≤ 10 mA |
| `GATE_OFF` | Full PWM with boost **disabled** | ≤ 10 mA — proves D3 gates the rail |
| `LOAD_MA` | 50% duty burst ≈ 1 s | 60–115 mA dry · 115–280 mA in water · < 10 mA = no disc (skip) |
| `VBAT_SAG` | Vbatt during the burst | info — real sag only on the cell |
| `GAUGE` | SoC gating on power source | on USB: reports "charging, SoC N/A" |
| `LED` | LED1 triple-blink | operator check — watch it |
| `BUTTON` | The trigger itself (or a 5 s press window) | press seen |
| `HANDOFF` | USB↔cell flip counter | info — needs the manual unplug test below |

The run ends with `VERDICT: PASS/FAIL (…counts…)`, one machine-parseable line (`SELFTEST,BATTERY_KIT,<fw>,<verdict>,<counts>` — grep `^SELFTEST,` when batch-logging), and the list of steps that still need hands: multimeter cross-check, the unplug/replug handoff, charge-LED behavior, the phone/radio test, and the duty sweep.

A short button press still toggles the mist (now on release); everything below remains for probing features one at a time.

## Checklist (serial @ 115200)

| Do | Verifies |
|---|---|
| press button | Button wiring — mist + LED toggle (hold ≥ 1.5 s = self-test) |
| `a` | Automated self-test (table above) |
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

### Power notes (as-built V0.4 June run · V0.4.1 July 2026 production run)

- **Boost rail at power-on differs by revision.** V0.4 (R8 pull-up): the ~5 V rail is **live from power-on** until the sketch drives D3 low in `setup()` — don't be surprised by 5 V on a scope before boot. **V0.4.1 (R8 pull-down): the rail is dead and LED2 stays OFF until firmware raises D3** — a dark LED2 at plug-in is correct, not a fault. The piezo stays off regardless (R11 holds the MOSFET gate down).
- **D8 logic-high moved:** ~2.6 V on V0.4 (R22 = 150 kΩ) → **~3.1 V on V0.4.1 (R22 = 220 kΩ)** — the spin widened the USB-detect margin from 0.2 V to 0.66 V.
- **Deep-sleep drain:** ~0.3–0.5 mA on V0.4 (R8 re-enables the boost when the GPIO releases) → **~0.25 mA on V0.4.1** (divider + LDO only).
- Charge current is **~196 mA** (R6 = 5.1 kΩ on the LP4060) — a deliberate thermal derate from the 500 mA design. LED1 (green) lit = charging; a 500 mAh cell takes ≈ 3 h from empty.
- The battery divider (R18/R19) is hard-wired and draws a constant **~210 µA** from the cell — negligible in use, but don't expect shelf-storage battery life with a cell left plugged in.

Pass everything? Move on to the [MistMaker library](https://github.com/owochel/MistMaker) (≥ 2.1.0) examples — `WiFiPhoneControl` re-enables graceful low-battery shutdown, now gated on the D8 mux status.
