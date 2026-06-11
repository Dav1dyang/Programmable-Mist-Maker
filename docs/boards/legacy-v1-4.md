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

## Firmware

Legacy example sketches:
[`legacy/v1-4/example-code/`](https://github.com/Dav1dyang/Programmable-Mist-Maker/tree/main/legacy/v1-4/example-code).
The [MistMaker library](../library.md) still supports this board:

```cpp
MistMaker mist(MistMakerLegacyV1());
```
