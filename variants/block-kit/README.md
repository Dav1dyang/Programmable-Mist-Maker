# Block Kit

A 3-PCB build of the Programmable Mist Maker designed around a modular "drop-in container" UX: insert the magnetic water container and misting starts automatically; lift it off and it stops. A single button provides manual override and LED brightness control.

## What's in this folder

| Subfolder | Contents |
|---|---|
| [`hardware/`](hardware/) | KiCad projects for the three PCBs (Driver, Mist & LED, Button) plus assembly notes |
| [`firmware/BlockKit_Production/`](firmware/BlockKit_Production/) | Daily-use firmware — full Phase A UX + WiFi onboarding + OTA + web UI for config and debug |
| [`firmware/BlockKit_BringUp/`](firmware/BlockKit_BringUp/) | Minimal per-feature test sketch — flash this when something on the bench feels off |
| `enclosure/` | (planned) 3D-printable shell |

## Key components

- **MCU:** Seeed XIAO ESP32-C6 (same as V1.4)
- **Mist drive:** TPS61023 3V3→5V boost + UCC27518 level shifter + DMT10H009LCG MOSFET → 108.7 kHz / 50 % PWM on a 16 mm ceramic disc
- **Power:** USB-C charging via LP4068BF, single-cell Li-Po; TPS2116DRLR mux switches between USB and battery; AP7361C 3V3 LDO
- **Sensing:** INA180A3 + 30 mΩ shunt for piezo current; reed switch for magnetic container-present detection
- **LEDs:** IS31FL3731 charlieplex driver with 14 LEDs (8 + 6) on the Matrix B side
- **Input:** single B3AL-1002P tactile switch on a dedicated PCB

## Bring-up sequence

1. Assemble the three PCBs and verify the inter-board harness (see [`hardware/README.md`](hardware/README.md)).
2. Apply the V0.1 reed-to-D10 blue-wire rework.
3. Flash `firmware/BlockKit_Production/BlockKit_Production.ino` — see [`firmware/BlockKit_Production/README.md`](firmware/BlockKit_Production/README.md) for full install steps (incl. the WiFiManager dependency for OTA + web UI). If a hardware feature looks broken, flash [`firmware/BlockKit_BringUp/`](firmware/BlockKit_BringUp/) first to verify each peripheral in isolation.
4. Walk the bring-up checklist in the firmware README.
5. With scope mode (`s`) on, gather a Serial Plotter trace of `mean_mA, var_mA2` for: full water, low water, dry disc, no disc. These traces drive the Phase B water-level classifier.

## Safety and care

The shared safety notes (cleaning, mineral buildup, what water to use) live in the [root README](../../README.md). They apply to every variant.

## Status

- ✅ V0.1 hardware: 3 PCBs designed
- ✅ Phase A firmware: this folder
- ⏳ Phase B firmware: water-level classifier from real bench data
- ⏳ V0.2 hardware: VBAT voltage divider for graceful low-battery stop, optional INA180 filter-corner raise
- ⏳ Enclosure: pending
