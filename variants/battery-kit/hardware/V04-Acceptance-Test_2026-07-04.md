# Battery Kit V0.4 — Acceptance Test Protocol

> **V0.4.1 boards (rev "0-4-1", R8 = pull-DOWN):** two expectations flip vs
> V0.4 — (1) at power-on the ~5 V rail is **DEAD** and **LED2 stays OFF** until
> the sketch starts (boost defaults off now); (2) deep-sleep battery drain
> should measure **~0.25 mA instead of ~0.5+** (divider + LDO only, boost truly
> off). D8 on USB now reads **~3.1 V** (R22 = 220 k). Everything else identical.

**Purpose:** confirm a V0.4 board is 100% functional, including the new ST
power-source sense and battery-reading accuracy. ~20 minutes per board.
**Firmware:** `BatteryKit_BringUp` from `main` (post PR #27). Serial 115200.
**Setup:** piezo disc in water, 1S cell connected, USB to computer, multimeter
handy (inline USB current meter helpful).

## A. Power & boot — 30 s

| Step | Do | Pass |
|---|---|---|
| A1 | Plug USB | `[BOOT] reset: power-on` · `[USB] boot source: USB (VIN1)` · `[BATT]` shows 3.0–4.4 V |
| A2 | Look at LEDs | LED2 (red) ON = 5 V rail up · LED1 (green) ON if cell not full (charging), OFF if full |

## B. Mist drive — 1 min

| B1 | `t` | Mist within ~1 s, D7 LED on |
| B2 | `0`…`9` | Plume visibly follows the level |
| B3 | Press button | Toggles mist (active-high) |

## C. Current sense / water detection — 2 min

`c` at the default duty — three distinct bands prove the sensor chain:

| Condition | Expected `c` |
|---|---|
| Disc in water | **130–200 mA** |
| Disc dry (briefly!) | 70–100 mA |
| Piezo unplugged | ~0–10 mA |

## D. ST pin (the V0.4 headline) — 3 min

| D1 | `u` on USB | "present : mux on VIN1 (USB)" — D8 pin ≈ 2.6–2.7 V on a meter, ST node (TP3) ≈ 4.4 V |
| D2 | Cell in, unplug USB | **Board keeps running** (mist continues if on). Meter D8 → ~0 V |
| D3 | Replug, press `u` | "handoffs since boot: ≥2 … handoff survived, no reset" |

D3 is the **mux handoff test** — it proves the TPS2116's 1.3 ms break-before-make
gap didn't brown out the MCU at normal load. (Serial dies during the unplug;
the counter + reset reason are the after-the-fact proof.)

## E. Battery reading accuracy — 5 min

| E1 | `b` on USB | Voltage + **`[USB - charging, SoC N/A]` tag** — the tag itself proves ST gating works |
| E2 | Multimeter on BATT+ (battery connector) vs `b` | Agree within **±0.05–0.10 V** (divider + ADC calibration) |
| E3 | Resting-voltage truth check on the cell | Run DutySweep_Test `B` (armed battery sweep) — its **duty-0 baseline row** logs resting Vbatt while on the cell; compare to multimeter before/after. ±0.05 V = calibrated |
| E4 | Charging behavior | LED1 ON with a partial cell; inline USB meter shows ~0.2 A above idle; LED1 OFF at full |

## F. Radio + phone — 5 min

| F1 | Flash `PhoneSensors` (WiFi creds + relay `mistcontrol.byproductlab.com`) | Maker appears in the web app's MAKERS panel with live mA/water/RSSI |
| F2 | Slider | Drives mist, watchdog cuts mist ~6 s after closing the app |
| F3 | Music mode | Mist pulses on beats (new build is live) |

## G. Optional: drive-stage health sweep — 3 min

DutySweep_Test, `w` at the default 600 mA limit. A healthy V0.4 matches the
reference curve: **~170–225 mA near-plateau from 18–50% duty**, then 310 (56%)
→ 480 (62%), abort ~68%. A plateau shifted far from this = suspect disc,
water level, or drive stage.

## Known NON-defects (expected on every V0.4)

- **LED3 (white) is dim** — 3.1 V Vf on a 3.3 V GPIO; V0.5 item.
- **~5 V rail live from power-on** until firmware boots (R8 pull-up); piezo stays off.
- **~0.3–0.5 mA drain in deep sleep** (R8 re-enables the boost when the GPIO
  releases); V0.5 item (R8 → pull-down).
- **~210 µA constant divider drain** on the cell (hard-wired R18/R19).
- D8 can't be watched over serial while on battery (USB *is* the serial link) —
  that's what the flip counter and TP3 are for.

**All boxes pass → the board is good.** File one row per board serial if
batch-testing.
