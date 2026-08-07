# MistMaker Library

**[MistMaker](https://github.com/owochel/MistMaker)** is the Arduino library for every
board in this project — PWM mist control, current sensing, battery monitoring, and
power-source awareness behind one friendly API. Treat mist like an LED.

## Install

Arduino IDE → **Library Manager** → search "MistMaker", or clone
[owochel/MistMaker](https://github.com/owochel/MistMaker) into your libraries folder.

Minimum version depends on your board: **v2.1+** for the
[Battery Kit](boards/battery-kit.md) and [Legacy V1.4](boards/legacy-v1-4.md),
**v2.2+** for the [Extension Kit](boards/extension-kit.md), **v2.5+** to drive a kit
from an [Arduino over jumper wires](labs/making-mist-with-an-arduino.md). Newest is
always safe.

!!! warning "v2.6 changed how hard the boards drive by default"
    The default duty cap dropped from 127 (50%) to **85 (33%)** — bench-tested as a
    good amount of mist with the tapped inductor staying cool on long runs. Because
    [`setLevel()` is a ratio of the cap](#how-hard-can-it-drive-measured), **every
    level now drives ~33% less than on v2.5.x**, not just the top end. To get the
    old output back, set the cap explicitly: `mist.setMaxDuty(127);`

Every tuning value the library assumes lives in one documented place:
`namespace MistMakerDefaults` at the top of `MistMaker.h`.

## Quick start

```cpp
#include <MistMaker.h>

// One line per board — pick yours:
MistMaker mist(MistMakerBatteryKitV041());  // current production board (V0.4: same preset)
// MistMaker mist(MistMakerBatteryKitV04());  // identical pins — either name works
// MistMaker mist(MistMakerBatteryKitV03()); // V0.3: battery sensing off by default
// MistMaker mist(MistMakerExtensionV01());
// MistMaker mist(MistMakerBlockKitV01());
// MistMaker mist(MistMakerLegacyV1());

void setup() {
  mist.begin();
  mist.turnOn();        // full mist
  mist.setLevel(128);   // half mist — dim it like an LED
}

void loop() {}
```

Custom wiring? Use the pin constructor: `MistMaker mist(mistPin, enPin, sensePin, ledPin);`

## Disc & water detection

The boards measure piezo current through a shunt + INA180A3. A missing disc, a dry
disc, and a disc in water each draw distinctly different current — one ADC pin gives
you disc detection *and* a water sensor for free.

!!! warning "Disc detection is solid; water level is not settled yet"
    **Disc presence is stable** — a missing or disconnected disc is a large, obvious
    current difference, and `MIST_DISC_MISSING` / `MIST_DISC_DISCONNECTED` are
    dependable on every board.

    **Water-level detection is still in development.** The margin between a wet disc
    and a low one is much narrower and it moves with disc wear, water depth, wick
    condition, and supply voltage — so `MIST_WATER_OK` / `MIST_WATER_LOW` need
    per-build testing and calibration before you rely on them. Treat them as a hint,
    not a float switch, and don't gate anything safety-critical on them.

```cpp
float ma = mist.readCurrentMa();        // live current in mA

MistSenseState s = mist.probe();        // brief probe, then restores output
switch (s) {
  case MIST_WATER_OK:          /* keep misting */          break;
  case MIST_WATER_LOW:         /* warn: refill soon */     break;
  case MIST_DISC_MISSING:      /* no piezo attached */     break;
  case MIST_DISC_DISCONNECTED: /* disc fell off mid-run */ break;
}
```

**Calibration** — bench-measured defaults ship with the library; adapt with:

```cpp
mist.autoCalibrateSense();              // run once, disc attached + in water
mist.setSenseThresholds(10.0, 110.0, 70.0);  // or hard-code (mA)
mist.setCurrentSenseFactor(3.0);        // different shunt/amp? gain × shunt
```

## USB or battery? (Battery Kit V0.4)

The V0.4 board routes the power mux's status pin to D8, so firmware always knows
the real power source:

```cpp
if (mist.onBattery()) { /* battery reading is a true state-of-charge */ }
if (mist.usbPresent()) { /* cell is charging; its voltage tracks the charger */ }
```

Battery monitoring **gates itself** on this: on USB, `batteryState()` returns
`MIST_BATT_CHARGING` and never `LOW`/`CRITICAL` — the hardware fix for V0.3's
false low-battery shutdowns. (Without an ST pin, `usbPresent()` is always true —
fail-safe: a board that can't sense its source will never blind-shut-down.)

## Battery monitoring (Battery Kit)

```cpp
float v   = mist.readBatteryVolts();   // calibrated, via on-board divider
uint8_t p = mist.batteryPercent();     // rough LiPo gauge for UIs

if (mist.batteryCritical()) {          // hysteresis built in; never fires on USB (V0.4)
  mist.shutdown();                     // mist off + boost rail off
  esp_deep_sleep_start();              // sleep instead of brown-out
}
```

Defaults: divider ratio 2.0, low = 3.45 V, critical = 3.20 V. Override with
`setBatteryDivider()` / `setBatteryThresholds()`.

## How hard can it drive? (measured)

The duty cap was bench-characterized on real V0.4 hardware (2026-07 sweep, 0→90% duty):

| Cap | What you get |
|---|---|
| **default (33% duty, cap 85)** | The thermal sweet spot: a good amount of mist with the tapped inductor staying cool over long runs; easily battery-sustainable. |
| 50% duty (cap 127) | The pre-2.6 default. More mist, still safe; ~0.23 A on the 5 V rail, ~0.3 A from the cell. `mist.setMaxDuty(127)`. |
| `mist.setMaxDuty(MistMakerDefaults::DUTY_TURBO)` (**~70%**) | The measured **true mist maximum** — ~4× the input power; wall adapter (≥ 2 A) territory. |
| above ~75% | Mist *declines* and turns unstable while current climbs — the library hard-limits at 90% of full scale. |

Your sketch's `setLevel(0..255)` scale is unaffected by the cap — 255 always means
"my current maximum."

!!! note "`setLevel()` is a ratio of the cap, not a clamp against it"
    The level is rescaled onto the cap — `duty = level / 255 × dutyMax` — so
    raising the cap makes **every** level stronger, not just the ones that used to
    clip:

    | `setLevel()` | duty at the default cap (85) | duty at `DUTY_TURBO` (178) |
    |---|---|---|
    | 255 | 85 (33%) | 178 (70%) |
    | 128 | 43 (~17%) | 89 (~35%) |
    | 64 | 21 (~8%) | 45 (~18%) |

    So `setLevel(128)` means "half of what this board is currently allowed to
    make," not a fixed duty. `setMaxDuty()` re-applies the current level
    immediately, so changing it mid-mist takes effect at once. Two edges worth
    knowing: `setLevel(0)` is special-cased to `turnOff()` (it drops the enable
    pin and LED, not just duty), and any nonzero level floors at `duty = 1`, so a
    low level can never silently round down to off.

## Examples

`File → Examples → MistMaker` — work through them in order:

| Example | What it shows |
|---|---|
| `Blink` | Hello-world: mist 6 s on / 3 s off, LED follows |
| `Breath` | Mist that breathes — smooth fade in, hold, fade out with `setLevel()` |
| `WaterDetect` | Self-minding mist: stops when water runs out or the disc comes off, resumes by itself |
| `PhoneDemo` | Phone mic / light / motion / face / **music** drive the mist over the internet ([relay + web app included](https://github.com/owochel/MistMaker/tree/main/extras/phone-app)) |

Retired examples (MQTT/Home Assistant, ESP-NOW, WiFi-AP control) live in the
[v2.1.0 release](https://github.com/owochel/MistMaker/releases/tag/v2.1.0).

## Home Assistant without code

Prefer YAML over C++? The
[ESPHome config](https://github.com/Dav1dyang/Programmable-Mist-Maker/tree/main/firmware-examples/home-assistant)
makes any board a native Home Assistant device with zero programming.
