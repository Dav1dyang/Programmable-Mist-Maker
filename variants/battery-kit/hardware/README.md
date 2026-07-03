# Battery Kit V0.3 — Hardware

| File | What |
|---|---|
| [`kicad/`](kicad/) | KiCad 10 project (schematic + PCB + custom symbol/footprint libs) |
| [`production/MistMaker-Battery-Kit_0-3.zip`](production/) | Gerbers for JLCPCB |
| [`production/bom.csv`](production/bom.csv) | BOM with LCSC part numbers (for JLCPCB SMT assembly) |
| [`production/positions.csv`](production/positions.csv) | Pick-and-place positions |

Schematic PDF export: pending (plot from KiCad: File > Plot > PDF).

## Version changes

**V0.4** (assembled 2026-06; KiCad files pending import — this folder holds V0.3):

- **TPS2116 ST (mux status) routed to XIAO D8** via an R15 pull-up + R21/R22
  (100k/150k) divider — HIGH = on USB, LOW = on the cell. The same node drives
  Q1 to disable the 3V3 LDO whenever USB is present, so the board LDO and the
  XIAO's own regulator never fight. This is the hardware fix for V0.3's
  false low-battery shutdowns on USB.
- **Charge current 500 mA → ~200 mA** (R6 2 kΩ → 5.1 kΩ) — keeps the SOT-23-5
  charger comfortably inside its thermal budget.

**V0.3:**

- **Battery voltage divider on D1** (`D1_BATT_VOLTAGE`, ratio 2.0) — enables the fuel gauge + graceful low-battery shutdown in firmware.

## Ordering from JLCPCB

1. Upload `MistMaker-Battery-Kit_0-3.zip` (defaults fine; 1.6 mm).
2. Enable **SMT assembly**, upload `bom.csv` + `positions.csv`.
3. Hand-solder afterwards: XIAO sockets, PH-2.0 piezo + battery connectors.

The KiCad project uses the bundled `Mist_Custom` symbol and `Mist_Library.pretty` footprints (paths in `sym-lib-table` / `fp-lib-table` are project-relative — it opens standalone).

## Key datasheets

- [TPS61023](https://www.ti.com/lit/ds/symlink/tps61023.pdf) — 5.5 V boost converter
- [TPS2116](https://www.ti.com/lit/ds/symlink/tps2116.pdf) — power mux (USB/battery)
- [LP4060B5F](https://datasheet.lcsc.com/lcsc/1912111437_LOWPOWER-LP4060B5F_C517259.pdf) — Li-Po charger
- [INA180](https://www.ti.com/lit/ds/symlink/ina180.pdf) — current-sense amp (A3 = 100 V/V)
- [UCC27511A](https://www.ti.com/lit/ds/symlink/ucc27511a.pdf) — gate driver
- [DMT10H009LCG](https://www.diodes.com/assets/Datasheets/DMT10H009LCG.pdf) — MOSFET
- [Seeed XIAO ESP32-C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)
