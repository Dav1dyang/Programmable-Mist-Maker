# Battery Kit V0.4.1 — Hardware

As-fabbed files for the **July 2026 production run** (the boards sold in the
shop). The KiCad project keeps the `V0-4` file name — the V0.4.1 changes are
edits inside it, and the production set here is the exact export that run was
built from.

| File | What |
|---|---|
| [`kicad/`](kicad/) | KiCad 10 project, **as-fabbed V0.4.1** (schematic + PCB + custom symbol/footprint libs) |
| [`MistMaker-Battery-Kit_V0-4-1_SCH.pdf`](MistMaker-Battery-Kit_V0-4-1_SCH.pdf) | Schematic PDF (plotted from the project) |
| [`production/MistMaker-Battery-Kit_0-4-1.zip`](production/) | Gerbers for JLCPCB — the ordered fileset |
| [`production/bom.csv`](production/bom.csv) | BOM with LCSC part numbers (for JLCPCB SMT assembly) |
| [`production/positions.csv`](production/positions.csv) | Pick-and-place positions |
| [`production/designators.csv`](production/designators.csv) / [`netlist.ipc`](production/netlist.ipc) | Designator map + IPC netlist from the same export |
| [`V04-Acceptance-Test_2026-07-04.md`](V04-Acceptance-Test_2026-07-04.md) | Per-board acceptance protocol (~20 min) — the automated slice runs in `BatteryKit_BringUp`'s self-test |
| [`PCB-Review_Battery-Kit-V0-4_2026-07-03_board-in-hand.md`](PCB-Review_Battery-Kit-V0-4_2026-07-03_board-in-hand.md) | Board-in-hand design review (ST sense verified, D8 margin analysis) |
| [`PCB-Review_Battery-Kit-V0-4_2026-06-22.md`](PCB-Review_Battery-Kit-V0-4_2026-06-22.md) | Pre-order design + procurement review |
| [`Duty-Cycle-Research_2026-07-03.md`](Duty-Cycle-Research_2026-07-03.md) | Measured duty→mist curve: why the 50% default, where the real maximum is (~70%) |

<!-- PHOTO: V0.4.1 assembled board, top — hardware close-up for this page -->

## Version changes

**V0.4.1** (July 2026 production run, 15 pcs — **current**):

- **R22 150 kΩ → 220 kΩ** — D8's USB-detect logic-high moves ~2.6 V → ~3.1 V,
  widening the margin over the XIAO's V<sub>IH</sub> from 0.2 V to 0.66 V (the one
  bench-marginal number population spread could have tipped across a batch).
- **R8 pull-up → pull-down (to GND)** — the ~5 V boost rail now boots **OFF**
  (LED2 dark until firmware raises D3) and deep-sleep drain drops from
  ~0.3–0.5 mA to **~0.25 mA**. Firmware-compatible: every drive path sets EN
  explicitly.
- **R7 2 kΩ → 5.1 kΩ** — LED2 dimmed to a sane brightness.
- Still deferred to V0.5: delete C14 (redundant bulk), copy the Extension's U4
  100 nF bypass placement, brighter LED3, footprint-name hygiene (IC2 carries a
  `TPS62932` footprint name; the part is a TPS2116 — electrically correct).

**V0.4** (assembled 2026-06, 5-pc prototype):

- **TPS2116 ST (mux status) routed to XIAO D8** via an R15 pull-up + R21/R22
  divider — HIGH = on USB, LOW = on the cell. The same node drives
  Q1 to disable the 3V3 LDO whenever USB is present, so the board LDO and the
  XIAO's own regulator never fight. This is the hardware fix for V0.3's
  false low-battery shutdowns on USB.
- **Charge current 500 mA → ~196 mA** (R6 = 5.1 kΩ) — keeps the SOT-23-5
  charger comfortably inside its thermal budget.

**V0.3:**

- **Battery voltage divider on D1** (`D1_BATT_VOLTAGE`, ratio 2.0) — enables the
  fuel gauge + graceful low-battery shutdown in firmware.
  (V0.3 KiCad files: see this folder's git history.)

## Ordering from JLCPCB

1. Upload `MistMaker-Battery-Kit_0-4-1.zip` (defaults fine; 1.6 mm).
2. Enable **SMT assembly**, upload `bom.csv` + `positions.csv`.
3. Hand-solder afterwards: XIAO sockets, PH-2.0 piezo + battery connectors.
4. Incoming boards: flash `BatteryKit_BringUp`, run the **automated self-test**
   (hold the button ≥ 1.5 s), then the manual steps in
   [`V04-Acceptance-Test_2026-07-04.md`](V04-Acceptance-Test_2026-07-04.md).

If you edit the schematic before ordering, re-export BOM + positions + gerbers
**as one set** (Fabrication Toolkit) — never mix a fresh zip with an old BOM.

The KiCad project uses the bundled `Mist_Custom` symbol and `Mist_Library.pretty`
footprints (paths in `sym-lib-table` / `fp-lib-table` are project-relative — it
opens standalone).

## Key datasheets

- [TPS61023](https://www.ti.com/lit/ds/symlink/tps61023.pdf) — 5.5 V boost converter
- [TPS2116](https://www.ti.com/lit/ds/symlink/tps2116.pdf) — power mux (USB/battery)
- [LP4060B5F](https://datasheet.lcsc.com/lcsc/1912111437_LOWPOWER-LP4060B5F_C517259.pdf) — Li-Po charger
- [INA180](https://www.ti.com/lit/ds/symlink/ina180.pdf) — current-sense amp (A3 = 100 V/V)
- [UCC27511A](https://www.ti.com/lit/ds/symlink/ucc27511a.pdf) — gate driver
- [DMT10H009LCG](https://www.diodes.com/assets/Datasheets/DMT10H009LCG.pdf) — MOSFET
- [Seeed XIAO ESP32-C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)
