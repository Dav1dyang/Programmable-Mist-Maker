# Block Kit — Hardware

The Block Kit is a 3-PCB stack. Each PCB has its own folder so KiCad projects, BOMs, and exported PDFs can evolve independently.

```
hardware/
├── driver-pcb/         # Block-Kit-V0-1            (XIAO socket, USB-C, charger, boost, MOSFET drive, INA180)
├── mist-and-led-pcb/   # Block-Kit-Mist-and-LED    (IS31FL3731, 14 LEDs, reed switch, piezo connector, Qwiic)
└── button-pcb/         # Block-Kit-Button          (single B3AL-1002P momentary switch)
```

## Inter-board wiring (V0.1)

The Driver PCB exposes two socket headers, `J7` and `J8`, that mate with the Mist & LED PCB. The Button PCB connects through a WAGO-2059 to the `SW+`/`SW−` pads on the Mist & LED PCB.

| Net | From | To | Notes |
|---|---|---|---|
| `+3V3` / `GND` / `+5V5` | Driver PCB | Mist & LED PCB | Power for IS31FL3731 (3V3) and piezo MOSFET drive (5V5) |
| `D0_MIST_PWM_5V5` | Driver PCB | Piezo (J1 on Mist & LED) | JP01 closed by default |
| `D1_REED` | Driver PCB | Reed switch SW1 | **V0.1 rework required:** blue-wire reed pads to a D1 breakout (see below) |
| `D2_C6` | INA180A3 (Driver) | n/a | Local on Driver PCB |
| `D3_LED` | Driver PCB | TPS61023 EN + LED2 (red) | Indicator LED lights when boost is enabled |
| `D4_SDA` / `D5_SCL` | Driver PCB | IS31FL3731 + Qwiic J6 | Shared I2C bus |
| `D6_BUTTON` | Driver PCB | Button PCB via SW+/SW− → J2 | Active-HIGH, 10 kΩ pull-down on Driver PCB |
| `D7_LED` | Driver PCB | LED3 (white) | Local on Driver PCB |

## V0.1 rework — reed switch on D1

The original V0.1 schematic routes the reed switch through `J2` into the button series circuit. The firmware needs the reed and button on **separate** GPIOs. For V0.1 boards:

1. Cut the trace from the reed switch to `SW+`/`SW−`.
2. Blue-wire one reed pad to a `D1` breakout (via J7/J8 socket or the Driver PCB's D1 test point).
3. Leave the other reed pad on GND.
4. Confirm jumper `JP02` is **open** so D1 is not driving PWM.

Document the actual rework with a photo in the next commit.

## V0.2 hardware items

- Battery voltage divider: 1 MΩ + 1 MΩ from `VBAT` to a freed ADC pin so firmware can warn at ~3.5 V and gracefully stop at ~3.3 V. Freeing a pin likely means moving the reed switch to an I2C IO expander.
- Optional: raise the INA180 RC filter corner from ~159 Hz to ~400 Hz (drop the 1 µF cap to ~0.4 µF, or drop the 1 kΩ resistor to ~400 Ω) if Phase A bench data shows the current variance signal is weak.
- Split-wire the reed and button properly in the schematic (no blue-wire rework needed).
