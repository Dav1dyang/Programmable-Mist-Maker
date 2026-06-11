# Programmable Mist Maker

**A simple open source, battery powered & programmable IoT mist making device** — by
[shuang cai](https://shuangcai.cargo.site/) and [David Yang](https://davidyang.work/)
at ByProduct Lab.

![Assembled mist maker](https://raw.githubusercontent.com/Dav1dyang/Programmable-Mist-Maker/main/assets/2025-05-22_V1-4_Assembled.jpg)

This [Open Source Hardware Certified](https://certification.oshwa.org/us002742.html)
project (OSHWA **US002742**) turns a piezo disc, a custom PCB, and any small container
into a programmable mist device. Most mist circuits online lack documentation — this
project fills that gap with tested reference circuits, full KiCad sources, an Arduino
library, and honest notes about what went wrong along the way.

First presented as a hands-on workshop at the **Open Hardware Summit 2025**.

## Start here

1. **Pick a board** — the [Extension Kit](boards/extension-kit.md) is the easiest
   first build; the [Battery Kit](boards/battery-kit.md) cuts the cord. Compare them
   under the **Boards** tab.
2. **Flash the BringUp sketch** for your board to verify the hardware feature by
   feature.
3. **Install the [MistMaker library](library.md)** (v1.1+) and work through the
   examples: `MistBlink` → `MistDimming` → `WaterDetect` → `WiFiPhoneControl`.
4. **Smart home?** The Home Assistant examples need no code at all — see the
   [library page](library.md#examples).

!!! tip "Don't want to solder?"
    Assembled and tested kits are available at
    **[shop.byproductlab.com](https://shop.byproductlab.com)** — buying one supports
    the open-source work.

## The boards at a glance

| Board | Power | Best for | Status |
|---|---|---|---|
| [Extension Kit](boards/extension-kit.md) | USB-C | First builds, desks, classrooms | V0.1 — hardware + firmware ready |
| [Battery Kit](boards/battery-kit.md) | Li-Po + USB-C | Portable builds, installations | V0.3 — hardware + firmware ready |
| [Block Kit](boards/block-kit.md) | Li-Po + USB-C | Drop-in container UX, 14 LEDs | V0.1 — demoed at OHS 2026 |
| [I2C MultiPack](boards/i2c-multipack.md) | TBD | Multi-emitter installations | In design |
| [Legacy V1.4](boards/legacy-v1-4.md) | USB / Li-Po | Reproducing the original workshop kit | Shipped, frozen |

## Safety & care

!!! warning "Water + electronics + time = biology"
    The device wicks water toward electronics with cotton sticks. Use distilled or
    clean tap water, clean the container regularly, and let cotton sticks dry between
    uses — careless long-term use can grow bacteria. Full notes in
    [How It Works](how-it-works.md#use-and-care).
