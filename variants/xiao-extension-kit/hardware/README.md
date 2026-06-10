# Extension Kit V0.1 — Hardware

| File | What |
|---|---|
| [`kicad/`](kicad/) | KiCad 10 project (schematic + PCB) |
| [`MistMaker-Seeed-Expansion_V0-1_SCH.pdf`](MistMaker-Seeed-Expansion_V0-1_SCH.pdf) | Schematic PDF — read this first |
| [`MistMaker-Seeed-Expansion-V0.1__Assembly.pdf`](MistMaker-Seeed-Expansion-V0.1__Assembly.pdf) | Assembly drawing |
| [`production/XiaoMistMaker_V0-1.zip`](production/) | Gerbers for JLCPCB |
| [`production/bom.csv`](production/bom.csv) | BOM with LCSC part numbers (for JLCPCB SMT assembly) |
| [`production/positions.csv`](production/positions.csv) | Pick-and-place positions |

## Ordering from JLCPCB

1. Upload `XiaoMistMaker_V0-1.zip` (defaults are fine; 1.6 mm, any color).
2. Enable **SMT assembly**, upload `bom.csv` + `positions.csv`.
3. Hand-solder afterwards: PH-2.0 piezo connector, XIAO headers.

## Key datasheets

- [INA180](https://www.ti.com/lit/ds/symlink/ina180.pdf) — current-sense amp (A3 = 100 V/V)
- [UCC27511A](https://www.ti.com/lit/ds/symlink/ucc27511a.pdf) — single-channel gate driver
- [DMT10H009LCG](https://www.diodes.com/assets/Datasheets/DMT10H009LCG.pdf) — MOSFET
- [Seeed XIAO ESP32-C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)
