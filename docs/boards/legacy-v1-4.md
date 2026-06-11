# Legacy V1.4

The original single-PCB workshop kit — shipped at the **Open Hardware Summit 2025**
workshop, now frozen. Preserved for anyone reproducing the kit exactly as documented;
for new builds, start with the [Extension Kit](extension-kit.md) or
[Battery Kit](battery-kit.md).

![V1.4 board](https://raw.githubusercontent.com/Dav1dyang/Programmable-Mist-Maker/main/assets/2025-05-22_V1-4_Board_Only.jpg)

## Interactive schematic

<script type="module" src="../../assets/vendor/kicanvas.js"></script>
<kicanvas-embed src="../../assets/hardware/MistMakerV1-4.kicad_sch" controls="basic"></kicanvas-embed>

[Full V1.4 files on GitHub →](https://github.com/Dav1dyang/Programmable-Mist-Maker/tree/main/legacy/v1-4)

## Key components

| Component | Function |
|---|---|
| Piezo disc (108.7 kHz) | Vibrates water to generate mist |
| Tapped inductor (3-legged) | Boosts voltage through LC resonance |
| AO3400A MOSFET | Switches circuit at high frequency |
| TPS61023 boost converter | Powers piezo from battery or USB |
| MCP73831 | Li-Po charging |
| Seeed XIAO ESP32-C6 | Controls mist and PWM |

## The famous duck

![Duck enclosure](https://raw.githubusercontent.com/Dav1dyang/Programmable-Mist-Maker/main/assets/Ducky_and_Container.jpg)

V1.4 pioneered the recycled-container approach — duck and UFO enclosure STLs live in
[`legacy/v1-4/EnclosureDesignMaterials/`](https://github.com/Dav1dyang/Programmable-Mist-Maker/tree/main/legacy/v1-4/EnclosureDesignMaterials).

## Known quirks & fixes

These issues are specific to V1.4 and were designed away in the V0.x boards (power
mux, dedicated boost rail, gate driver, no OTA-on-boot). If you're reproducing this
board, the original workarounds:

| Issue | Fix |
|---|---|
| Mist fails on battery | Bypass the XIAO's 3.3 V regulator with an external boost converter — the onboard regulator handles USB fine but not battery |
| Uploading code fails while misting | Add a pull-down to the MOSFET gate; disable the boost converter during uploads |
| Startup delay | Disable OTA; add a delay between enabling the TPS61023 and activating the mist PWM |

!!! note "Power sequence matters (V1.4 only)"
    When developing on this board, always connect **USB first, then battery** — if
    battery power is applied before USB, the serial port may not enumerate in the
    Arduino IDE. Once serial is up, battery can be connected freely. Standalone,
    either power source works alone. (The V0.x Battery Kit's TPS2116 power mux
    eliminates this entirely.)

## Firmware

Legacy example sketches:
[`legacy/v1-4/example-code/`](https://github.com/Dav1dyang/Programmable-Mist-Maker/tree/main/legacy/v1-4/example-code).
The [MistMaker library](../library.md) still supports this board:

```cpp
MistMaker mist(MistMakerLegacyV1());
```
