# Programmable Mist Maker

**Open-source, programmable ultrasonic mist maker kits** — by
[shuang cai](https://shuangcai.cargo.site/) & [David Yang](https://davidyang.work/) at ByProduct Lab.

📖 **[Documentation](https://docs.byproductlab.com/)** ·
🛒 **[Buy a kit](https://shop.byproductlab.com)** ·
📜 [OSHWA certified US002742](https://certification.oshwa.org/us002742.html) ·
✍️ [Build story on Hackster](https://www.hackster.io/dav1dyang/waste-into-wonder-making-programmable-mist-makers-fe3ae7)

![Assembled mist maker board with piezo disc and Li-Po battery](assets/2025-05-22_V1-4_Assembled.jpg)

Most mist-maker circuits online are undocumented black boxes. This project fills that
gap: tested reference circuits, full KiCad sources, an Arduino library, and honest
notes about what went wrong along the way — so creators, educators, and engineers can
explore mist as a material. First presented as a hands-on workshop at the
**Open Hardware Summit 2025**.

## The boards

| Board | Power | Best for | Status |
|---|---|---|---|
| [**Extension Kit**](variants/xiao-extension-kit/) | USB-C | First builds, desks, classrooms | V0.1 — hardware + firmware ready |
| [**Battery Kit**](variants/battery-kit/) | Li-Po + USB-C | Portable builds, installations | V0.3 — hardware + firmware ready |
| [**Block Kit**](variants/block-kit/) | Li-Po + USB-C | Drop-in container UX, 14 LEDs | V0.1 — demoed at OHS 2026 |
| [**I2C MultiPack**](variants/i2c-multipack-kit/) | TBD | Multi-emitter installations | In design |
| [**Legacy V1.4**](legacy/v1-4/) | USB / Li-Po | Reproducing the original workshop kit | Shipped, frozen |

Each variant folder has its own README with pin maps, an order-ready BOM + JLCPCB
production files, and build instructions. Board-by-board guides with **interactive
schematics** live on the [documentation site](https://docs.byproductlab.com/).

## Start here

1. **Pick a board** — the [Extension Kit](https://docs.byproductlab.com/boards/extension-kit/)
   is the easiest first build; the [Battery Kit](https://docs.byproductlab.com/boards/battery-kit/)
   cuts the cord. (Or [buy one assembled and tested](https://shop.byproductlab.com).)
2. **Flash the variant's BringUp sketch** to verify the hardware feature by feature.
3. **Install the [MistMaker Arduino library](https://github.com/owochel/MistMaker)** (v1.1+)
   and work through the examples: `MistBlink` → `MistDimming` → `WaterDetect` →
   `WiFiPhoneControl` (control it from your phone).
4. **Smart home?** [`firmware-examples/home-assistant/`](firmware-examples/home-assistant/)
   joins Home Assistant with no code at all (ESPHome), or use the `HomeAssistant_MQTT`
   library example.

## How it works

An ESP32 drives a piezo disc at its **108.7 kHz** resonance through a MOSFET and a
3-legged tapped inductor — an autotransformer + LC tank that steps 5 V up to the
~80 Vpp the disc needs, while a current-sense amp lets the firmware detect the
disc and the water level on one ADC pin. The full story — atomization science, why
that strange inductor is everywhere in Shenzhen and nowhere at DigiKey, and a
verified alternative circuit — is at
[docs.byproductlab.com/how-it-works](https://docs.byproductlab.com/how-it-works/).

## Repo map

| Path | Contents |
|---|---|
| `variants/<board>/` | Per-board KiCad project, BOM, production files, BringUp firmware, enclosure |
| `legacy/v1-4/` | The original workshop kit, preserved as documented (incl. duck & UFO enclosure STLs) |
| `firmware-examples/` | ESPHome / Home Assistant configs |
| `assets/` | Photos and media |

> [!NOTE]
> Reproducing the **legacy V1.4 board**? It has a few quirks the V0.x boards designed
> away (power sequencing, upload-while-misting) — fixes are preserved on the
> [Legacy V1.4 docs page](https://docs.byproductlab.com/boards/legacy-v1-4/).

## Use and care

> [!WARNING]
> Water + electronics + time = biology. Use distilled or clean tap water, clean the
> container regularly, and let cotton wick sticks dry between uses. Never run a disc
> dry for long, and never drive the circuit without a disc attached. Full notes in the
> [docs](https://docs.byproductlab.com/how-it-works/#use-and-care).

## License & credits

Open source under the **MIT License**, certified open source hardware
([OSHWA US002742](https://certification.oshwa.org/us002742.html)). Designed and
tested by [David Yang](https://davidyang.work/) and
[shuang cai](https://shuangcai.cargo.site/), inspired by community
reverse-engineering and YouTube teardowns — full reference list in the
[docs](https://docs.byproductlab.com/how-it-works/#research-references).

Parts of the documentation were drafted with AI assistance; all technical content is
validated against bench measurements and datasheets.

**Contact:** [contact@byproductlab.com](mailto:contact@byproductlab.com) ·
[@dav1dyang](https://www.instagram.com/dav1dyang/)
