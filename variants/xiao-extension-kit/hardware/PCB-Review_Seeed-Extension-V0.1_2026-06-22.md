# PCB Review — MistMaker Seeed (XIAO) Extension V0.1

**Date:** 2026-06-22  **Reviewer:** Claude Code (automated audit)  **Scope:** Full design review — BOM + every LCSC value, footprints, ERC/DRC, circuit sanity.
**Audited artifact:** on-disk saved KiCad files.
**Tools:** `kicad-cli` 10.0.1 (BOM/ERC/DRC/netlist export), Diode `pcb` 0.4.0 (`pcb scan` datasheet reader), LCSC product-page verification, custom footprint-geometry parser, TI/Diodes/Nexperia datasheets.
**What it is:** a Seeed XIAO (ESP32-C6) carrier that drives a piezo mist atomizer via a low-side MOSFET + autotransformer, with high-side current sense and a Qwiic I²C port.

---

## 1. Verdict

**Fab-ready, no buildability defects.** All 17 BOM lines map to the correct LCSC part/value/package; all placed footprints are geometrically correct; DRC shows 0 unconnected pads / 0 footprint errors. The findings are refinements and intent-confirmations.

### Action items (ranked)

| # | Priority | Item |
|---|----------|------|
| 1 | **Should-fix** | **U3 (UCC27511A gate driver) is missing its bulk VDD cap.** Only a single 0.1 µF (C5) on VDD/5 V; the datasheet calls for **0.1 µF + ~1 µF** for an 8 A-sink driver. Add a ~1 µF low-ESR cap right at U3 VDD(1)–GND(4). Matters more here because VDD = 5 V is only ~0.8 V above the driver's UVLO. |
| 2 | **Confirm** | **VBUS must stay ≥ ~4.7 V under mist load.** The driver runs at 5 V (XIAO VBUS) with only ~0.8 V of UVLO margin; a sagging USB source could drop it into UVLO and kill the gate drive. Verify on the bench. |
| 3 | **Confirm** | **Current sense saturates at ~1.09 A.** INA180A3 (gain 100) runs off **3.3 V** with the 30 mΩ shunt → 3.0 V/A, clipping at ~1.09 A. Confirm the atomizer's peak current is below that; if not, run the INA off 5 V or use the gain-50 INA180A2. |
| 4 | **Confirm intent** | **U1 (XIAO 2×7 header) and J1 (Qwiic) are `dnp yes` + `in_bom no`** — deliberately excluded. Correct *if* the XIAO is soldered directly (footprint is `XIAO-Add-On_No_Edge`, castellated) and the Qwiic port is intentionally unpopulated. If you wanted a removable socket and/or a live I²C port, those parts are **not currently on the order** — clear their DNP/BOM flags. |
| 5 | **Confirm (firmware)** | **The mist PWM is on GPIO0**, an ESP32-C6 **strapping pin**. A transient high at boot could briefly pulse the atomizer (driver is non-inverting). Drive GPIO0 low early in firmware, or move PWM off GPIO0 in a respin. |
| 6 | **Confirm** | **No on-board I²C pull-ups** (SDA/SCL only reach the XIAO and the Qwiic connector). Fine *iff* the Qwiic peripheral provides them (SparkFun Qwiic boards do). If you ever add an on-board I²C device, fit 4.7 kΩ pull-ups. |
| 7 | **Hygiene** | **Footprint Q3 is misnamed** `DMP3007SCG7` (a SOT-23 part) but is correctly a **9-pad DFN3×3-8** for the DMT10H009. Rename + confirm symbol pin-mapping. |
| 8 | **Hygiene** | **Broken library source paths** — `Mist_Custom` points at a stale `MistMakerV1-8` folder; `PH-2PWB` lib not in config. Symbols are cached so the board builds, but fix `sym-lib-table` to re-enable editing. Also a **net-label typo: `MOSFET_DRIAN`** (→ `MOSFET_DRAIN`). |
| 9 | **Verify (topology)** | **"5V5_MIST" is not a regulated 5.5 V boost** — it's an autotransformer/flyback piezo driver (D1 is a clamp/steering diode, not a boost rectifier). Confirm the PWM frequency matches the atomizer's resonance, and scope Q3's drain to confirm the ring stays < 100 V. Consider an RC snubber pad on Q3 drain (none present). |

---

## 2. BOM + LCSC verification — all 17 lines

Every line checked against its LCSC product page. **Result: 0 wrong parts, 0 package mismatches.**

| Ref | Value | LCSC | Real part (MPN / Mfr) | Pkg ✓ | LCSC stock | Notes |
|---|---|---|---|---|---|---|
| C1 | 0.1 µF | C14663 | CC0603KRX7R9BB104 / YAGEO (50 V X7R) | ✓ | OOS* | |
| C2 | 1 µF | C14445 | CL05A105KP5NNNC / Samsung (0402, 10 V) | ✓ | 29,200 | CS RC-filter cap |
| C5 | 100 nF | C3011705 | GRM188R72A104KA35J / muRata (0603, 100 V X7R) | ✓ | 4,320 | U3 VDD bypass (see action #1) |
| D1 | PMEG10010ELRX-TP | C7603319 | PMEG10010ELRX / Nexperia (SOD-123FL, 100 V 1 A) | ✓ | 46,440 (Ext) | Flyback clamp |
| J3 | PH-2PWB | C2905019 | PH-2PWB / DEALON (2-pin 2 mm conn) | ✓ | 20,140 | Piezo output |
| L1 | 3-Legged Inductor | C49338546 | XRCD75-250K/801K / XR (dual-winding 25 µH/800 µH) | ✓ | 6,160 | Autotransformer; verify 3-leg pinout |
| LED2 | Red | C25503345 | XL-1005SURC / XINGLIGHT (0402) | ✓ | 628,900 | Status indicator |
| Q3 | DMT10H009LCG-7 | C461105 | DMT10H009LCG-7 / Diodes (**VDFN3333-8**, 100 V 47 A) | ✓ (footprint **misnamed**) | 313 (Ext) | Land is correct 9-pad DFN |
| R1 | 30 mΩ | C7467247 | FRM121WFR030TM / FOJAN (1206, 1 W) | ✓ | 38,400 | Current-sense shunt |
| R2 | 10 Ω | C22859 | 0603WAF100JT5E / UNI-ROYAL | ✓ | OOS* | Gate **turn-on** R (OUTH) |
| R3 | 2.2 Ω | C22939 | 0603WAF220KT5E / UNI-ROYAL | ✓ | OOS* | Gate **turn-off** R (OUTL) |
| R4 | 100 kΩ | C14675 | RC0603FR-07100KL / YAGEO | ✓ | 100 | Q3 gate pulldown |
| R5 | 1 kΩ | C106235 | RC0402FR-071KL / YAGEO | ✓ | OOS* | CS RC filter (with C2) |
| R7 | 510 Ω | C25123 | 0402WGF5100TCE / UNI-ROYAL | ✓ | OOS* | Driver input/enable network |
| R20 | 22 Ω | C25092 | 0402WGF220JTCE / UNI-ROYAL | ✓ | OOS* | Series gate resistor |
| U2 | INA180A3 | C122882 | INA180A3IDBVR / TI (SOT-23-5, gain 100) | ✓ | 48,355 (Ext) | Runs off 3.3 V → ~1.09 A range |
| U3 | UCC27511ADBV | C2676982 | UCC27511ADBVR / TI (SOT-23-6 gate driver) | ✓ | 13,340 | |

**Not in BOM but on the PCB (intentionally excluded — confirm per action #4):** `U1` XIAO 2×7 header (`in_bom no`, `dnp yes`), `J1` Qwiic JST-SH (`in_bom no`, `dnp yes`), `JP5V5` solder jumper, `LOGO1` silkscreen graphic.

\* **OOS = LCSC *retail* out of stock** — jellybean 1 % chips / basic caps that are JLCPCB **Basic**-class and typically available for *assembly*. Verify at order time; not a defect.

---

## 3. Footprint audit (geometry-verified from the PCB)

All placed lands are physically correct. One misnamed:

| Ref | Footprint name in board | Actual geometry | Real part package | Status |
|---|---|---|---|---|
| Q3 | `DavidLib_Switch:DMP3007SCG7` | 9 pads, 0.65 mm pitch, ~2×3 mm | DMT10H009 = VDFN3333-8 | ✓ correct land, **wrong name** |

All others verified (U2 SOT-23-5, U3 SOT-23-6, D1 SOD-123FL, J3 PH2PWB connector, L1 3-leg, U1 XIAO 2×7 header, passives sized right). `kibuzzard-*` = silkscreen art (0 pads), correctly not in BOM.

---

## 4. ERC / DRC

**DRC: 4 violations — all `lib_footprint_mismatch (Local override)`** (J3, Q3, LED2, L1) = library drift, benign. **0 unconnected pads, 0 footprint errors.**

**ERC: 16 (10 errors, 6 warnings)** — nothing structural:
- *Errors:* 7× `pin_not_connected` on the XIAO header's unused GPIOs (expected — not every XIAO pin is used); U2 V+ / IN+ "power pin not driven" (missing PWR_FLAG annotation — INA is on XIAO3V3); LOGO1 graphic pin.
- *Warnings:* `PH-2PWB` lib not in config + `Mist_Custom` stale path (action #8); `multiple_net_names` aliases on the autotransformer legs (`5V5_MIST`=`L_LEG1`, `L_LEG3`=`PIEZO-`, `L_LEG2`=`MOSFET_DRIAN`) — mostly intentional, but note the **`MOSFET_DRIAN` typo**; SolderJumper symbol mismatch.

---

## 5. Design review (circuit sanity)

| Subsystem | Finding | Severity |
|---|---|---|
| **Mist driver (D1, L1, Q3)** | 5 V → R1 shunt → D1 → L1 center-tap; L1 outer legs → Q3 drain + piezo (J3); Q3 source → GND. This is an **autotransformer/flyback piezo driver PWM'd by the XIAO via U3**, *not* a regulated 5.5 V boost — D1 is a clamp, not a rectifier. No output/snubber cap on the switch node. | NOTE — verify resonance & drain ring < 100 V |
| **Gate drive (U3 → Q3)** | OUTH→R2(10 Ω) turn-on / OUTL→R3(2.2 Ω) turn-off → R20(22 Ω) → Q3 gate; R4 100 k pulldown. R_on 32 Ω / R_off 24.2 Ω — split-output used correctly. **Only 0.1 µF on VDD** (wants 0.1 µF + 1 µF). VDD 5 V ≈ 0.8 V over UVLO. | **WARN (decap)** + NOTE (UVLO) |
| **Current sense (U2 INA180A3)** | High-side 30 mΩ shunt, gain 100 → **3.0 V/A**; **Vs = 3.3 V → clips at ~1.09 A**; CS lands on XIAO A2 (ADC-capable ✓), RC-filtered (R5 1 k + C2 1 µF). | NOTE (range) |
| **I²C** | SDA/SCL reach only the XIAO + Qwiic J1 — **no on-board pull-ups**. OK iff the Qwiic peripheral supplies them. | NOTE |
| **XIAO interface (U1)** | PWM = GPIO0 (**strapping pin** — guard at boot), CS = GPIO2/A2 (ADC ✓), SDA/SCL on header pins 5/6, VBUS pin14, 3V3 pin12, GND pin13. Mapping is sane. | NOTE (GPIO0) |
| **Q3 / D1 ratings** | Q3 100 V DMT10H009 and D1 100 V PMEG10010 — both 100 V, consistent with an expected high autotransformer ring. Fully enhanced at 5 V VDD. | OK |

---

## 6. Cross-board consistency (vs Battery-Kit V0.4)

Shared parts use the **same LCSC code on both boards** (INA180A3 C122882, UCC27511A C2676982, DMT10H009 C461105, PH-2PWB C2905019, 30 mΩ C7467247, PMEG10010 C7603319, 3-leg inductor C49338546) — good.
**One difference to confirm intentional:** the INA180 current-sense runs off **3.3 V here (range ~1.09 A)** vs **~5 V on the Battery board (range ~1.6 A)**. If both boards must measure the same atomizer current, the lower ceiling here is the binding limit.

---

## 7. Caveats

- LCSC "stock" is retail/time-sensitive; JLCPCB *assembly* stock differs (see §2 note).
- The mist-driver topology interpretation is from netlist tracing + datasheets; confirm against your reference atomizer-driver design and on the bench (drain ring, resonance).
- Current/headroom figures use nominal Vf/derating; verify the marginal items on hardware.
- Audit reflects the **saved** files; re-export if there are unsaved KiCad edits.
