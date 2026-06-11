# Block Kit — V0.1

A 3-PCB build of the Programmable Mist Maker designed around a modular **"drop-in
container" UX**: insert the magnetic water container and misting starts automatically;
lift it off and it stops. A single button provides manual override and LED brightness
control. Demoed at the Open Hardware Summit 2026.

## The three PCBs

| PCB | Role |
|---|---|
| **Driver** | XIAO ESP32-C6, power path, piezo drive |
| **Mist & LED** | Piezo disc + IS31FL3731 charlieplex driver with 14 LEDs (8 + 6) |
| **Button** | Single B3AL-1002P tactile switch on a dedicated board |

[KiCad projects & assembly notes on GitHub →](https://github.com/Dav1dyang/Programmable-Mist-Maker/tree/main/variants/block-kit/hardware)

## Key components

- **MCU:** Seeed XIAO ESP32-C6 (same as V1.4)
- **Mist drive:** TPS61023 3V3→5V boost + UCC27518 level shifter + DMT10H009LCG
  MOSFET → 108.7 kHz / 50% PWM on a 16 mm ceramic disc
- **Power:** USB-C charging via LP4068BF, single-cell Li-Po; TPS2116DRLR mux switches
  between USB and battery; AP7361C 3V3 LDO
- **Sensing:** INA180A3 + 30 mΩ shunt for piezo current; **reed switch** for magnetic
  container-present detection
- **Input:** single tactile switch on a dedicated PCB

## Bring-up sequence

1. Assemble the three PCBs and verify the inter-board harness (see the
   [hardware README](https://github.com/Dav1dyang/Programmable-Mist-Maker/tree/main/variants/block-kit/hardware)).
2. Apply the V0.1 reed-to-D10 blue-wire rework.
3. Flash
   [`BlockKit_Production`](https://github.com/Dav1dyang/Programmable-Mist-Maker/tree/main/variants/block-kit/firmware/BlockKit_Production)
   — full Phase A UX + WiFi onboarding + OTA + web UI. If a hardware feature looks
   broken, flash `BlockKit_BringUp` first to verify each peripheral in isolation.
4. Walk the bring-up checklist in the firmware README.
5. With scope mode (`s`) on, gather a Serial Plotter trace of `mean_mA, var_mA2` for:
   full water, low water, dry disc, no disc. These traces drive the Phase B
   water-level classifier.

## Status

- ✅ V0.1 hardware: 3 PCBs designed
- ✅ Phase A firmware: production firmware with web UI + OTA
- ⏳ Phase B firmware: water-level classifier from real bench data
- ⏳ V0.2 hardware: VBAT voltage divider for graceful low-battery stop
- ⏳ Enclosure: pending

Safety and care notes live in [How It Works](../how-it-works.md#use-and-care).
