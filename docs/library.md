# MistMaker Library

**[MistMaker](https://github.com/owochel/MistMaker)** is the Arduino library for every
board in this project — PWM mist control, current sensing, and battery monitoring
behind one friendly API. Treat mist like an LED.

## Install

Arduino IDE → **Library Manager** → search "MistMaker" (v1.1+), or clone
[owochel/MistMaker](https://github.com/owochel/MistMaker) into your libraries folder.

## Quick start

```cpp
#include <MistMaker.h>

// One line per board — pick yours:
MistMaker mist(MistMakerBatteryKitV03());
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

## Battery monitoring (Battery Kit)

```cpp
float v   = mist.readBatteryVolts();   // via on-board divider
uint8_t p = mist.batteryPercent();     // rough LiPo gauge for UIs

if (mist.batteryCritical()) {          // hysteresis built in
  mist.shutdown();                     // mist off + boost rail off
  esp_deep_sleep_start();              // sleep instead of brown-out
}
```

Defaults: divider ratio 2.0, low = 3.45 V, critical = 3.20 V. Override with
`setBatteryDivider()` / `setBatteryThresholds()`.

## Examples

`File → Examples → MistMaker` — work through them in order:

| Example | What it shows |
|---|---|
| `MistBlink` | Hello-world: 6 s ON / 3 s OFF cycle |
| `MistDimming` | Organic "breathing" mist with `setLevel()` |
| `WaterDetect` | Disc + water detection, auto-calibration, auto-recovery |
| `WiFiPhoneControl` | Phone control via WiFi AP + web UI, graceful low-battery shutdown |
| `HomeAssistant_MQTT` | Native Home Assistant device via MQTT Discovery |

## Home Assistant without code

Prefer YAML over C++? The
[ESPHome config](https://github.com/Dav1dyang/Programmable-Mist-Maker/tree/main/firmware-examples/home-assistant)
makes any board a native Home Assistant device with zero programming.
