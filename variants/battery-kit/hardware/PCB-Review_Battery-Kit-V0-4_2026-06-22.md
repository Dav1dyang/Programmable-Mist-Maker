# PCB Review — MistMaker Battery-Kit V0-4

**Date:** 2026-06-22  **Reviewer:** Claude Code (automated audit)  **Scope:** Full design review — BOM + every LCSC value, footprints, ERC/DRC, circuit sanity.
**Audited artifact:** on-disk saved KiCad files (note: project lock files were present, so the board may be open in KiCad — re-export if you have unsaved edits).
**Tools:** `kicad-cli` 10.0.1 (BOM/ERC/DRC/netlist export), Diode `pcb` 0.4.0 (`pcb scan` datasheet reader), LCSC product-page verification, custom footprint-geometry parser, TI/Diodes datasheets.

---

## 1. Verdict

**Fab-ready, no buildability defects.** All 37 BOM lines map to the correct LCSC part/value/package. All placed footprints are geometrically correct. No copper/connectivity errors (DRC: 0 unconnected, 0 footprint errors). The power architecture is sound and **V0.4 resolves the V0.3 USB-only false-low-battery shutdown** (mux is correctly USB-priority).

The findings below are **refinements, hygiene, and procurement** — nothing blocks a build, but several are worth fixing before a production run.

### Action items (ranked)

| # | Priority | Item |
|---|----------|------|
| 1 | **Should-fix** | **C14 (10 µF 0402, 10 V — C307415)** sits on the BAT node (~4.2 V). A 10 V X5R 0402 derates heavily under DC bias (effective ≪ 10 µF). Use a 16/25 V part, or rely on C5 (0603) as the real bulk. Also lowest-stock cap (1,880). |
| 2 | **Should-fix** | **Charger thermal:** 500 mA (R6 = 2 kΩ) on the LP4060 **SOT-23-5** ≈ 1.05 W at a depleted cell (2.9 V) vs 0.5 W package rating. It self-throttles (safe) but runs hot and won't deliver full current until the cell is >~3.7 V. Consider 300–400 mA (R6 ≈ 2.5–3.3 kΩ) for a small cell, and/or more copper. |
| 3 | **Should-fix** | **U4 (UCC27511A gate driver) has no local VDD bypass** — only shared 22 µF bulk on the 5 V net. Add 0.1–1 µF directly across U4 VDD(1)–GND(4). |
| 4 | **Should-fix** | **LED3 (white) marginal:** driven from a 3.3 V GPIO through 150 Ω into a white LED with Vf ≈ 3.1 V (C51933306) → ~0.2 V headroom; dim and Vf-tolerance-sensitive. Drive from 5 V (via transistor) or accept it as a faint indicator. |
| 5 | **Confirm intent** | **J7 & J8** (a second pair of 1×07 headers) are `in_bom no` **but `dnp no`** — placed, not-DNP, yet hidden from the BOM (their sisters J1/J10 *are* in the BOM). Resolve: mark DNP if unpopulated, or add to BOM if they should be stuffed. |
| 6 | **Hygiene** | **Two footprints are misnamed** (lands are correct): `Q4` uses footprint `DMP3007SCG7` (a SOT-23 part name) but is correctly a **9-pad DFN3×3-8**; `IC2` uses `TPS62932DRLR` but is correctly an **8-pad SOT-583**. Rename to the real parts and confirm symbol pin-mapping. |
| 7 | **Hygiene** | **Broken library source paths** — custom symbol/footprint libs (`PH-2PWB`, `XEL4030-102MEC`, `Switch_DavidLib`) point at a *different* Google-Drive account (`davidyangemail@gmail.com`). Symbols are cached so the board builds, but you can't re-sync/edit. Fix `sym-lib-table`/`fp-lib-table` paths. |
| 8 | **Hygiene** | ERC error: a **dangling bus-to-wire entry** at (−105.41, 95.25). Clean up the stray wire. |
| 9 | **Procurement** | Secure before production: **U2 AP7361C (only 240 in LCSC stock)**, **L2 Coilcraft XEL4030 (Extended/special-order)**. Most parts are JLCPCB **Extended** (per-part feeder fees) — only Q1 (2N7002) is Basic. Many jellybean passives show LCSC-retail OOS but are JLC-Basic-class and typically available for assembly — verify at order time, don't panic. |
| 10 | **Optional** | Boost lands at **4.95 V** (low end of a 5.0–5.5 V intent). Raise R12 (~824 kΩ → 5.5 V) if you want margin. Optional ~220 pF feedforward cap across R12 for phase margin (COUT ≈ 44 µF). |

---

## 2. BOM + LCSC verification — all 37 lines

Every line was checked against its LCSC product page (manufacturer P/N, value, package, stock). **Result: 0 wrong parts, 0 package mismatches.** "Pkg ✓" = the schematic footprint matches the real part's package.

| Ref(s) | Value | LCSC | Real part (MPN / Mfr) | Pkg ✓ | LCSC stock | Notes |
|---|---|---|---|---|---|---|
| R1 | 30 mΩ | C7467247 | FRM121WFR030TM / FOJAN (1206, 1 W) | ✓ | 38,400 | Current-sense shunt |
| R2 | 10 Ω | C22859 | 0603WAF100JT5E / UNI-ROYAL | ✓ | OOS* | |
| R3 | 2.2 Ω | C22939 | 0603WAF220KT5E / UNI-ROYAL | ✓ | OOS* | |
| R4,R15 | 30 kΩ | C138008 | RC0402FR-0730KL / YAGEO | ✓ | OOS* | Mux PR1 top divider (R4) |
| R5 | 5.1 kΩ | C25905 | 0402WGF5101TCE / UNI-ROYAL | ✓ | OOS* | CHRG LED limit |
| R6,R7 | 2 kΩ | C4109 | 0402WGF2001TCE / UNI-ROYAL | ✓ | OOS* | **R6 = charger PROG (→500 mA)** |
| R8,R14,R16,R18,R19 | 10 kΩ | C25744 | 0402WGF1002TCE / UNI-ROYAL | ✓ | OOS* | R8 = boost EN pull-up; R16 = mux PR1 bottom |
| R9 | 150 Ω | C25082 | 0402WGF1500TCE / UNI-ROYAL | ✓ | OOS* | LED3 series |
| R10 | 1 kΩ | C106235 | RC0402FR-071KL / YAGEO | ✓ | OOS* | |
| R11,R17 | 100 kΩ | C14675 | RC0603FR-07100KL / YAGEO | ✓ | 100 | R11 = Q4 gate pulldown |
| R12 | 732 kΩ | C137940 | RC0402FR-07732KL / YAGEO | ✓ | OOS* | **Boost FB top** |
| R13,R21 | 100 kΩ | C25741 | 0402WGF1003TCE / UNI-ROYAL | ✓ | OOS* | **R13 = boost FB bottom** |
| R20 | 22 Ω | C25092 | 0402WGF220JTCE / UNI-ROYAL | ✓ | OOS* | Series gate resistor |
| R22 | 150 kΩ | C25755 | 0402WGF1503TCE / UNI-ROYAL | ✓ | OOS* | |
| C1 | 0.1 µF | C14663 | CC0603KRX7R9BB104 / YAGEO (50 V X7R) | ✓ | OOS* | LDO out bypass |
| C5,C6,C12,C15,C16 | 10 µF | C96446 | CL10A106MA8NRNC / Samsung (0603, **25 V** X5R) | ✓ | 195,780 | Bulk — comfortable rating |
| C7,C8,C9 | 22 µF | C98190 | CL21A226MOQNNNE / Samsung (0805, 16 V X5R) | ✓ | 79,500 | Boost in/out bulk |
| C10 | 1 µF | C14445 | CL05A105KP5NNNC / Samsung (0402, 10 V) | ✓ | 29,200 | |
| C13,C14 | 10 µF | C307415 | CL05A106MP8NUB8 / Samsung (0402, **10 V** X5R) | ✓ | 1,880 | ⚠ **10 V at BAT node — see action #1** |
| D1 | PMEG10010ELRX-TP | C7603319 | PMEG10010ELRX / Nexperia (SOD-123FL, 100 V 1 A Schottky) | ✓ | 46,440 (Ext) | Flyback clamp |
| Q1 | 2N7002 | C8545 | 2N7002 / JSCJ (SOT-23, 60 V N-MOSFET) | ✓ | OOS* (JLC **Basic**, 466 k) | |
| Q4 | DMT10H009LCG-7 | C461105 | DMT10H009LCG-7 / Diodes (**VDFN3333-8**, 100 V 47 A) | ✓ (footprint **misnamed**) | 313 (Ext) | Land is correct 9-pad DFN; rename footprint |
| IC1 | TPS61023 | C919459 | TPS61023DRLR / TI (SOT-563 boost) | ✓ | 34,120 (Ext) | |
| IC2 | TPS2116DRLR | C3235557 | TPS2116DRLR / TI (**SOT-583** mux) | ✓ (footprint **misnamed**) | 17,303 (Ext) | Symbol confirmed TPS2116; rename footprint |
| J1,J10 | Conn 1×07 socket | C27985232 | 1×7 2.54 mm female socket | ✓ | 6,512 | |
| J2,J3 | PH-2PWB | C2905019 | PH-2PWB / DEALON (2-pin 2 mm conn) | ✓ | 20,140 | |
| J6 | Qwiic | C2845372 | JST-SH-compat 1×04 1.0 mm | ✓ | 10,350 | Check gender vs mating cable |
| L1 | 3-Legged Inductor | C49338546 | XRCD75-250K/801K / XR (dual-winding 25 µH/800 µH) | ✓ | 6,160 | Verify 3-leg pinout vs datasheet |
| L2 | XEL4030-102MEC | C5441341 | XEL4030-102MEC / Coilcraft (1 µH, 9 A) | ✓ | not shown | **TI-recommended boost inductor**; likely Extended |
| LED1 | Green | C965793 | XL-1005UGC / XINGLIGHT (0402) | ✓ | 852,450 | Charge-status indicator |
| LED2 | Red | C25503345 | XL-1005SURC / XINGLIGHT (0402) | ✓ | 628,900 | 5 V-present indicator |
| LED3 | White | C51933306 | GL0402UW01 / Guiguang (0402, Vf 3.1 V) | ✓ | 144,950 | ⚠ Vf headroom — see action #4 |
| S2 | TS-1088-AR02016 | C720477 | TS-1088-AR02016 / XUNPU (SMD tactile) | ✓ | 754,070 | |
| U1 | LP4060B5F | C517259 | LP4060B5F / LOWPOWER (SOT-23-5, Li-ion charger) | ✓ | 910 (Ext) | 4.2 V float |
| U2 | AP7361C-3.3V | C151007 | AP7361C-33FGE-7 / Diodes (DFN-8 3×3, 3.3 V 1 A LDO) | ✓ | **240** (Ext) | Lowest stock — secure early |
| U3 | INA180A3 | C122882 | INA180A3IDBVR / TI (SOT-23-5, gain 100) | ✓ | 48,355 (Ext) | |
| U4 | UCC27511ADBV | C2676982 | UCC27511ADBVR / TI (SOT-23-6 gate driver) | ✓ | 13,340 | |

\* **OOS = out of stock on LCSC *retail*.** These are jellybean 1 % chip resistors / a basic cap — almost all are JLCPCB **Basic** parts that remain available for *assembly* even when LCSC retail shows zero. Verify against JLCPCB's assembly inventory at order time; not a BOM defect.

---

## 3. Footprint audit (geometry-verified from the PCB)

Every placed footprint was parsed from the `.kicad_pcb` and classified by pad count + pitch, then compared to the real part's package. **All lands are physically correct.** Two are misnamed:

| Ref | Footprint name in board | Actual geometry | Real part package | Status |
|---|---|---|---|---|
| Q4 | `DavidLib_Switch:DMP3007SCG7` | 9 pads, 0.65 mm pitch, ~2×3 mm | DMT10H009 = VDFN3333-8 | ✓ correct land, **wrong name** (DMP3007 is SOT-23) |
| IC2 | `DavidLib_Mux:TPS62932DRLR` | 8 pads, 0.50 mm pitch, ~1.5×1.5 mm | TPS2116DRLR = SOT-583 | ✓ correct land, **wrong name** (TPS62932 is a buck) |

All other ICs/passives/connectors verified correct (U1 SOT-23-5, U2 DFN-8 3×3, U3 SOT-23-5, U4 SOT-23-6, IC1 SOT-563, Q1 SOT-23, D1 SOD-123FL, L1 3-leg, J-sockets, etc.).
The `kibuzzard-*` "footprints" are silkscreen-art labels (0 pads) — correctly excluded from the BOM.

> **Why naming matters:** KiCad fabricates whatever pads are in the board file — it never cross-checks them against the package the LCSC code ships in. The lands here are right, but a footprint named after a *different* part is a real maintenance hazard (future edits may trust the name). Rename to the actual part.

---

## 4. ERC / DRC

**DRC: 10 violations — all `lib_footprint_mismatch (Local override)`** (J7, D1, C10, R10, R22, J8, LED3, Q1, S2, Q4). These mean the placed footprint was locally edited vs its library copy — benign for a self-contained board, but a symptom of the library drift in action #7. **0 unconnected pads, 0 footprint errors.**

**ERC: 22 (4 errors, 18 warnings).** Nothing structural:
- *Errors:* dangling bus-to-wire entry (action #8); TP1 pin-not-connected (test point — expected); IC1 VIN & H1 "power pin not driven" (missing PWR_FLAG annotation — the netlist confirms IC1 VIN is fed by PSW_VOUT, so this is cosmetic).
- *Warnings:* LP4060 (U1) pins typed "Unspecified" → `pin_to_pin` warnings (symbol hygiene — set proper pin types); several `multiple_net_names` aliases (`XIAOVIN`=`MODE`, `5V5_MIST`=`L_LEG1`, `L_LEG2`=`PIEZO+`, `L_LEG3`=`PIEZO-`, `BATT-`=`2N7002_S`) — mostly intentional labels on the autotransformer legs, but confirm none is an accidental merge; broken-library warnings (action #7).

---

## 5. Design review (circuit sanity)

**Rail chain:** USB/battery → LP4060 charger → BATT+; **TPS2116 mux** (VIN1=USB, VIN2=BATT+) → PSW_VOUT → feeds **both** the TPS61023 boost (→ ~4.95 V) **and** the AP7361C LDO (→ 3.3 V). Boost/LDO run from the mux output (battery-or-USB), not from the boosted rail — intentional and correct.

| Subsystem | Finding | Severity |
|---|---|---|
| **Charger (U1 LP4060)** | R6 = 2 kΩ → I_BAT = 1000/R = **500 mA**, 4.2 V float. Correct for Li-Po. But PD ≈ 1.05 W at VBAT 2.9 V vs 0.5 W SOT-23-5 rating → self-throttles, runs hot. | OK / **WARN (thermal)** |
| **BAT decoupling** | C14 (10 µF 0402 **10 V**) + C5 (10 µF 0603 25 V). C14 derates hard at 4.2 V. | **WARN** |
| **Power mux (IC2 TPS2116)** | MODE=VIN1 → priority mode; PR1 = R4 30 k / R16 10 k → VIN1 threshold 4.0 V. USB @5 V → PR1 = 1.25 V > VREF → **USB selected & passed through**. **Resolves the V0.3 false-low-batt shutdown.** | **OK ✓** |
| **Boost (IC1 TPS61023)** | FB R12 732 k / R13 100 k, VREF 0.595 V → **Vout = 4.95 V** (low end of 5–5.5 V). L2 = 1 µH (exact TI-recommended). CIN 42 µF / COUT 44 µF in range. No feedforward cap (optional). | OK / NOTE |
| **LDO (U2 AP7361C-3.3)** | Fixed 3.3 V, EP grounded, CIN 42 µF / COUT 10 µF+0.1 µF. Dropout ~0.34 V @1 A → tight on a near-empty battery, fine on USB. | OK / NOTE |
| **Current sense (U3 INA180A3)** | High-side, gain 100, 30 mΩ → **3.0 V/A**; Vs ≈ 4.95 V → max measurable **~1.6 A**; shunt 0.075 W (1206) fine. | OK / NOTE (range) |
| **Gate drive (U4 → Q4)** | OUTH→R2(10 Ω) / OUTL→R3(2.2 Ω) → R20(22 Ω) → Q4 gate; R11 100 k pulldown. R_on 32 Ω / R_off 24.2 Ω. R20 shared swamps the driver's 4 A/8 A asymmetry. **No local VDD cap.** Q4 100 V hugely over-rated for the ~5 V-rail flyback. D1 a proper clamp. | OK / **WARN (decap)** |
| **LEDs** | LED1 green = charge-status (~0.5 mA, dim, cosmetic); LED2 red = 5 V-present (~1.5 mA, OK); **LED3 white ~0.2 V headroom (3.1 V Vf on 3.3 V GPIO)**. | OK / **WARN (LED3)** |

---

## 6. Cross-board consistency (vs Seeed Extension V0.1)

Shared parts use the **same LCSC code on both boards** — good: INA180A3 (C122882), UCC27511A (C2676982), DMT10H009 (C461105), PH-2PWB (C2905019), 30 mΩ shunt (C7467247), PMEG10010 (C7603319), 3-leg inductor (C49338546).
One behavioral difference to confirm intentional: the **INA180 runs off ~5 V here (range ~1.6 A) but off 3.3 V on the Extension board (range ~1.1 A)**.

---

## 7. Caveats

- LCSC "stock" is retail and time-sensitive; JLCPCB *assembly* stock differs (see note under §2).
- LED Vf and exact derated capacitance values are nominal/datasheet — confirm on hardware for the marginal items (LED3, C14).
- The audit reflects the **saved** files; if the board is open in KiCad with unsaved changes, re-run the export.
- Raw exports (BOM CSVs, ERC/DRC reports, netlists, datasheet markdowns) are in the audit scratch dir and can be regenerated with `kicad-cli` 10.0.1.
