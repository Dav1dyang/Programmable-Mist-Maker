# PCB Review — MistMaker Battery-Kit V0-4 (board-in-hand)

**Date:** 2026-07-03  **Reviewer:** Claude Code (Fable)  **Scope:** As-built verification of the received V0.4 boards + component-by-component wiring review with emphasis on the new **TPS2116 ST → XIAO D8** power-source sense, post-order design drift, JLC part re-check, and status of the 2026-06-22 action items.
**Method:** `kicad-cli` 10.0.1 netlist/ERC/DRC on local copies; netlist-level diff between the **ordered** design (backup `2026-06-22_162548`, matching `production/` fab outputs) and the **current** saved files (edited 2026-06-24 and 2026-07-01); TI TPS2116 datasheet (`pcb scan`) for ST/MODE/PR1 semantics. Companion doc: `PCB-Review_Battery-Kit-V0-4_2026-06-22.md` (full 37-line LCSC verification — still valid, BOM unchanged except R6/R7 noted below).

---

## 1. Verdict

**The board you received is safe to bring up.** DRC on the ordered layout: 10 benign footprint-name warnings, 0 connectivity errors (same as the June audit). Netlist connectivity of the current schematic is **identical** to what was fabbed — no wiring changed after the order. The ST→D8 subsystem is correctly designed and datasheet-verified: **D8 HIGH = USB, LOW = battery**, matching library v1.2.0 semantics.

### What changed after the order (design drift)

| Item | Ordered (= your physical board) | Current sch (Jul 1) | Meaning |
|---|---|---|---|
| **R6** (LP4060 PROG) | 5.1 kΩ (C25905) → **~196 mA charge** | same | Fixed *before* order — June action #2 (charger thermal) is RESOLVED on your board |
| **R7** (LED2 series) | **2 kΩ** (C4109) → LED2 ~1.5 mA | **5.1 kΩ** (C25905) | Post-order cosmetic dimming; your board has the brighter red LED |
| PCB copper / constraints | — | unchanged (early DRC scare was a missing `.kicad_dru` in the test copy, not a design change) | — |

---

## 2. ST → D8 power-source sense (the V0.4 headline) — VERIFIED ✓

**Chain:** IC2 pin 8 (ST, open-drain) —[R15 30 k pull-up to XIAOVIN]— node `/ST` (also Q1 gate, TP3) —[R21 100 k]— node `D8_ST` (J10.6 = XIAO D8) —[R22 150 k]— GND.

- **Datasheet (TPS2116 §7.3.3):** ST pulled **low when VIN1 (USB) is not in use** — i.e., released when USB powers the output. With the pull-up: ST high on USB, low on battery.
- **USB present:** XIAOVIN ≈ 5 V → D8 = 5 × 150k/(30k+100k+150k) ≈ **2.68 V = HIGH**. On battery: ST asserted low *and* the pull-up rail itself is dead → **0 V = LOW, doubly guaranteed**, with zero battery drain through the network (I_ST leak 0.03 µA).
- **MODE** tied to VIN1 → priority mode ✓. **PR1** divider R4/R16 = 30k/10k, V_REF = 1.0 V → USB selected above **4.0 V** ✓.
- **Q1 (2N7002) side-effect — document this:** ST high (USB) turns Q1 on, pulling **U2 (AP7361C LDO) EN low → board LDO OFF on USB**; the XIAO then runs from its own onboard regulator. On battery, Q1 releases and R17 (100 k → PSW_VOUT) enables the LDO. Elegant: the two 3.3 V regulators never fight.
- **Firmware rule (already in v1.2.0 bring-up):** D8 must be `INPUT` with **no internal pull** — a ~45 k internal pull-up to 3.3 V would put the battery-state node at an indeterminate ~1.9 V.

### ⚠ Thin margin (works, fix in V0.5)
On USB, D8 ≈ 2.68 V vs ESP32-C6 V_IH ≈ 0.75 × 3.3 = **2.48 V — only ~0.2 V margin**, less if the XIAO 5 V pin sits below 5.0 V. Bench-verified working (2026-07-02), but for V0.5 retune, e.g. **R22 150 k → 220 k** → D8 ≈ 3.14 V @ 5 V in (still < 3.3 V abs-max at 5.25 V VBUS: 3.30 V).

### Break-before-make caution (bring-up test)
Switchover disconnects both inputs for ~1.3 ms. USB unplug while running = mux output dip + LDO restart via Q1/R17. Buffered by C6/C12/C7 (42 µF) + C15/C1 on 3V3. **Bring-up must include a live-unplug/replug test** to confirm no MCU reset.

---

## 3. Component-by-component review (current files ≡ as-built except R7)

| Subsystem | Parts | Finding |
|---|---|---|
| **Charger** | U1 LP4060B5F, R6 5.1 k (PROG), LED1 grn + R5 5.1 k (CHRG, open-drain) | ✓ ~196 mA — thermally comfortable for SOT-23-5 (June's 500 mA concern fixed). Charger VCC on XIAOVIN → unpowered (no drain) on battery. |
| **Power mux** | IC2 TPS2116, R4/R16 (PR1), R15/R21/R22 (ST), Q1+R17 (LDO gate) | ✓ Verified above. Footprint land = correct SOT-583 (name still says `TPS62932DRLR` — hygiene). |
| **LDO** | U2 AP7361C-3.3, C15 10 µF + C1 0.1 µF out, EP grounded | ✓. Dropout ~0.34 V @ 1 A → 3V3 sags on a near-empty cell (< ~3.64 V) — inherent, acceptable. |
| **Boost** | IC1 TPS61023, L2 XEL4030-102, R12/R13 FB (→ 4.95 V), R8 10 k EN pull-up to PSW_VOUT, CIN C6/C12/C7 = 42 µF, COUT C8/C9 = 44 µF | ✓. **Note: EN pulled HIGH → the ~5 V mist rail is live from power-on** before any firmware runs (piezo stays off because U4 input idles low through the XIAO's boot-time high-Z + gate pulldowns — see gate drive). Firmware should still drive D3 explicitly. |
| **Gate drive** | U4 UCC27511A (VDD on 5 V), OUTH R2 10 Ω / OUTL R3 2.2 Ω → R20 22 Ω → Q4 gate, R11 100 k pulldown | ✓ wiring. **Still no local 0.1–1 µF at U4 VDD** (June #3, open) — nearest bulk is C8/C9. Works, but add in V0.5 for clean 108.7 kHz edges. |
| **Mist tank** | 5 V → R1 30 mΩ → D1 PMEG10010 (series) → L1 3-leg autotransformer; Q4 switches leg 2; piezo across legs 2–3 (J3); leg breakouts J12/J13 | ✓ single-transistor resonant drive; D1 blocks resonant kickback into the boost. Q4 (100 V) generously rated for the flyback ring. |
| **Current sense** | R1 → U3 INA180A3 (V+ = 3V3, inputs on 5 V rail) → R10 1 k + C10 1 µF RC → D2 | ✓. Full-scale ~1.1 A (3.3 V/3.0 V-per-A); RC gives ~160 Hz smoothing — matches library averaging. |
| **Battery ADC** | R18/R19 10 k/10 k, BATT+ → D1_BATT_VOLTAGE | ✓ ratio 2.0 = library default. **Two design notes:** (a) divider is hard-wired → **~210 µA constant drain** (≈ 5 mAh/day; dominates deep-sleep budgets — fine for this product's duty cycle, but a V0.5 candidate: high-side switch or ST-gated divider); (b) **no filter cap on the ADC tap** — readings ride on mist-load sag; library's 16-sample averaging + hysteresis compensates. |
| **Button** | S2, COM → 3V3, NO → D6 + R14 10 k pulldown | ✓ **active-HIGH** (pressed = 3.3 V). Bring-up/library docs must match. |
| **LEDs** | LED1 grn (charge), LED2 red (5 V rail via JP5V5, R7), LED3 wht (D7 GPIO, R9 150 Ω, J11 jumper) | LED1/2 ✓. **LED3 still marginal** (June #4, open): 3.1 V V_f on a 3.3 V GPIO → expect *dim*; treat as faint indicator or fix in V0.5 (drive from 5 V via Q1-style transistor). |
| **Connectors** | J1/J10 XIAO socket; J7/J8 mirror pads (not stuffed, excluded from BOM, `dnp no` — June #5 still open as hygiene); J2 battery, J3 piezo (PH-2PWB); J6 Qwiic (3V3/GND/D4/D5) ✓; TP2 PR1, TP3 ST, TP4 gate, TP6 PSW_VOUT; J4/J5/J9/J11–J15 single-pin breakouts | ✓ Pin map confirmed: **D0 mist-PWM, D1 batt-ADC, D2 current-sense, D3 boost-EN, D4/D5 I²C, D6 button, D7 LED3, D8 ST** — exactly `MistMakerBatteryKitV04()`. |

**ERC:** 24 warnings/errors — same profile as June (dangling bus entry at (−105.41, 95.25) still present = June #8 open; TP1 no-connect; PWR_FLAG cosmetics; LP4060 unspecified pin types). Nothing structural.

---

## 4. June 22 action items — scorecard

| # | Item | Status on your physical board |
|---|---|---|
| 1 | C14 10 µF **10 V** 0402 on BAT node (C307415) | **OPEN** — ordered as-is. Derates hard at 4.2 V; C5 (25 V 0603) is the real bulk. Harmless for bring-up; use 16/25 V part in V0.5. |
| 2 | Charger thermal (500 mA) | **FIXED before order** — R6 = 5.1 k → ~196 mA. |
| 3 | U4 local VDD bypass | **OPEN** — no new cap in ordered or current files. V0.5. |
| 4 | LED3 white headroom | **OPEN** — unchanged. Expect dim. V0.5. |
| 5 | J7/J8 BOM/DNP hygiene | **OPEN** (cosmetic) — unstuffed on the build, so no physical issue. |
| 6 | Footprint renames (Q4 `DMP3007SCG7`, IC2 `TPS62932DRLR`) | **OPEN** (hygiene) — lands verified correct; rename to real packages. |
| 7 | Library-table paths → wrong Drive account | **OPEN** — `sym-lib-table`/`fp-lib-table` still reference the personal-account paths; migrate to the kicad-libraries repo nicknames. |
| 8 | Dangling bus-to-wire ERC | **OPEN** — still at (−105.41, 95.25). |
| 9 | Procurement (U2 stock 240, U1 910, L2 extended) | **STANDING** — re-check before the next run. |
| 10 | Boost 4.95 V → raise to 5.5 V option | **NOT TAKEN** — fine as-is; revisit only if mist output underwhelms. |

## 5. JLC parts check

BOM is the June-verified 37-line set (all LCSC numbers confirmed correct then; **no part-number changes since**) with two value moves inside already-verified parts: R6 → C25905 (5.1 k, shared with R5) on the build; R7 → C25905 in the current sch (build has C4109 2 k). The bulk-cap error that briefly existed in the repo's V0.3 BOM (**C25804** = a 10 kΩ resistor!) is **not** in the V0.4 order — C96446 (10 µF 25 V) was correctly ordered. Ordered CPL/BOM (`production/bom.csv`, `positions.csv`, 2026-06-22 16:25) match the ordered schematic exactly.

## 6. Recommended V0.5 delta (collected)

1. R22 150 k → 220 k (D8 HIGH margin).
1b. **R8: boost-EN pull-up → pull-DOWN.** During ESP32 deep sleep the D3 GPIO
   releases and R8 (10 k → PSW_VOUT) re-enables the boost, so "graceful
   low-battery deep-sleep" still leaks ~0.3–0.5 mA through the idle 5 V rail.
   Default-off EN (pull-down to GND) fixes the drain *and* the rail-live-at-boot
   quirk; D3 (GPIO21) isn't an RTC GPIO on the C6, so firmware gpio-hold isn't a
   reliable substitute. (From the 2026-07-02 firmware review.)
2. 0.1–1 µF at U4 VDD pins 1–4.
3. C13/C14 → 16/25 V 10 µF (or drop C14, keep C5).
4. LED3: transistor drive from 5 V (or accept faint).
5. Battery divider: gate with a P-FET or size up (drain ~210 µA); add 100 nF at the ADC tap.
6. Hygiene: rename Q4/IC2 footprints, DNP-mark J7/J8, delete dangling bus entry, fix lib-table paths, set LP4060 symbol pin types.
7. Port the R7 = 5.1 k LED2 change into the next fab (already in current sch).
