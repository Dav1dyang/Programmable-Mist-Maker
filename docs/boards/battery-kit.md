# Battery Kit — V0.4.1

The portable, single-PCB "grab and go" Mist Maker: Li-Po battery + USB-C charging,
full power path, piezo drive, current sensing, a button, a status LED — and
**trustworthy battery monitoring**: since V0.4 the board tells the firmware whether
it is actually running from USB or from the cell, so low-battery logic can never
false-trigger while you're plugged in. **V0.4.1** is the July 2026 production
revision — same design, with the power-detect margin widened and the piezo rail
now defaulting **off** at power-on (details in
[Revision history](#revision-history)).

**Great for:** installations, performances, kits, and anywhere without a USB cable.

!!! tip "Buy it assembled"
    [Battery Kit at shop.byproductlab.com](https://shop.byproductlab.com/kits/battery-kit) —
    every board arrives assembled and tested.

<!-- PHOTO: V0.4.1 board, top side, straight-on — assets/battery-kit-v041-top.jpg -->
<!-- PHOTO: V0.4.1 board, bottom side — assets/battery-kit-v041-bottom.jpg -->
<!-- PHOTO: close-up of USB-C + battery connector corner — assets/battery-kit-v041-power-corner.jpg -->

## Interactive schematic

<script type="module" src="../../assets/vendor/kicanvas.js"></script>
<kicanvas-embed src="../../assets/hardware/MistMaker-Battery-Kit-V0-4.kicad_sch" controls="basic"></kicanvas-embed>

*The embed shows the as-fabbed V0.4.1 design (the KiCad project keeps the V0-4
file name; the V0.4.1 changes are in it).*
[KiCad project & production files on GitHub →](https://github.com/Dav1dyang/Programmable-Mist-Maker/tree/main/variants/battery-kit/hardware)

## How it works

```
USB-C 5V ──────────────┬────────────► TPS2116 power mux ──► PSW_VOUT
Li-Po ─► LP4060 charger┘                   │        │
                                           │        ├─► TPS61023 boost (EN = D3) ─► ~5V rail
                        ST (status) ───────┘        └─► AP7361C 3V3 LDO ─► XIAO 3V3
                         │                               ▲ (auto-disabled on USB)
                         ├─► ÷ divider ─► D8  "am I on USB or the cell?"
                         └─► Q1 ─► LDO EN
5V ─► 30 mΩ shunt ─► UCC27511 ─► DMT10H009 MOSFET ─► 3-leg inductor ─► piezo disc
      │                ▲ D0: 108.7 kHz PWM
      └─► INA180A3 ─► D2 (analog current sense)
VBAT ─► ½ divider ─► D1 (analog fuel gauge)
```

- **Power path:** the TPS2116 mux picks USB when present (threshold 4.0 V),
  battery otherwise; the LP4060 charges the Li-Po from USB-C at a thermally
  gentle **~200 mA** (green LED = charging).
- **The ST trick (V0.4):** the mux's status pin does double duty — divided down
  to **D8** it tells firmware which source is live (HIGH = USB, LOW = cell), and
  through Q1 it **disables the board's 3V3 LDO whenever USB is present**, so the
  XIAO's own regulator and the board LDO never fight.
- **Piezo rail:** the TPS61023 boost (enabled by D3) makes a stable ~5 V from
  either source. Turn it off when idle — it's the single biggest standby drain.
- **Sensing:** INA180A3 + 30 mΩ shunt → analog D2 for disc/water detection;
  battery divider → D1 for the fuel gauge.

## Pin map

| XIAO pin | Net | Function |
|---|---|---|
| D0 | `MIST_PWM_3V3` | 108.7 kHz PWM to gate driver |
| D1 | `D1_BATT_VOLTAGE` | Battery voltage via ½ divider |
| D2 | `D2_CS` | INA180A3 current-sense output |
| D3 | `D3_TPS_EN` | TPS61023 boost enable (HIGH = piezo rail on) |
| D4 / D5 | `D4_SDA` / `D5_SCL` | I2C + Qwiic connector |
| D6 | `D6_BUTTON` | Button, active HIGH (10k pull-down on PCB) |
| D7 | `D7_LED` | Status LED |
| D8 | `D8_ST` | **Power-mux status (V0.4+):** HIGH = on USB, LOW = on the cell. Read as plain `INPUT` — the on-board divider sets the level |
| D9–D10 | — | Spare breakout |

## Key components

| Part | Role |
|---|---|
| LP4060B5F | Single-cell Li-Po USB-C charging (~200 mA) |
| TPS2116DRLR | USB/battery power mux + source status (ST → D8) |
| AP7361C-3.3 | 3V3 LDO (battery operation; auto-off on USB) |
| TPS61023 | Boost for the ~5 V piezo rail |
| UCC27511A + DMT10H009LCG | Gate driver + MOSFET, 108.7 kHz switching |
| 3-legged tapped inductor (CD75) | LC voltage boost to ~80 Vpp |
| INA180A3 + 30 mΩ shunt | Analog current sense (3.0 V/A) |
| TS-1088 tactile switch, white LED | User button + status |
| Qwiic / JST-SH 4-pin | I2C expansion |

## Battery monitoring that can't cry wolf (V0.4)

On USB the battery node tracks the *charger*, not the state of charge — V0.3
firmware reading it blindly caused false low-battery shutdowns. V0.4 fixes this in
hardware: D8 says which source is live, and the
[MistMaker library](../library.md) (v2.1+) gates itself on it —
`batteryState()` returns `CHARGING` on USB and only ever `LOW`/`CRITICAL` when the
cell is really the source.

| Voltage (on the cell, under load) | Meaning | Firmware should |
|---|---|---|
| 4.2 V | Full | — |
| ~3.7 V | Nominal | — |
| < 3.45 V | Low | Warn (LED blink / UI banner) |
| < 3.20 V | Critical | Mist off → boost off → deep sleep |

The `WiFiPhoneControl` and `HomeAssistant_MQTT` examples implement the full
graceful power-off (two consecutive critical readings, radio off, deep sleep).

## Revision history

| Rev | Status | What changed |
|---|---|---|
| **V0.4.1** | **Current — July 2026 production run, bench-validated** | R22 150k → 220k: D8's USB-detect logic-high moves ~2.6 V → ~3.1 V (margin 0.2 V → 0.66 V). R8 pull-up → pull-down: the ~5 V piezo rail now boots **OFF** until firmware raises D3, and deep-sleep drain drops to ~0.25 mA. R7 → 5.1 kΩ (LED2 dimming). |
| V0.4 | Prototype (June 2026, 5 pcs) | TPS2116 ST → D8 mux-status divider (R21/R22) — the "battery monitoring that can't cry wolf" hardware. Charge current derated to ~196 mA (R6 = 5.1 kΩ). |
| V0.3 | Prototype | First single-PCB battery board. No mux-status pin — firmware must not trust the battery reading on USB ([the V0.3 lesson](#battery-monitoring-that-cant-cry-wolf-v04)). |

Still open for V0.5: delete C14 (redundant bulk), copy the Extension's U4
100 nF bypass placement, brighter LED3, footprint-name hygiene.

## Power budget & mist ceiling (measured)

From the 2026-07 bench characterization ([full story on the library page](../library.md)):

- **Default drive (50% duty):** ~0.23 A on the 5 V rail, ~0.3 A from the cell —
  runs cool, sustainable on any supply. This is the library default.
- **Peak mist (~70% duty, `DUTY_TURBO`):** ~0.83 A on the rail — wall-adapter
  territory (≥ 2 A recommended); a fully-charged cell can just reach it.
- The mux switches sources **break-before-make (~1.3 ms)** — a graceful handoff
  at normal loads (bench-verified: unplug mid-mist, no reset), but at extreme
  drive (> ~75% duty) a collapsing supply can't be caught. Practical takeaway:
  the board's supply architecture comfortably covers everything up to peak mist.

## Build your own

1. Order PCBs with the JLCPCB production files in
   [`hardware/`](https://github.com/Dav1dyang/Programmable-Mist-Maker/tree/main/variants/battery-kit/hardware)
   (SMT assembly recommended).
2. Solder the XIAO sockets, piezo connector (PH-2.0), and battery connector (PH-2.0).
3. Snap in a XIAO ESP32-C6, connect a 1S Li-Po (500 mAh+ recommended).
4. Flash
   [`BatteryKit_BringUp`](https://github.com/Dav1dyang/Programmable-Mist-Maker/tree/main/variants/battery-kit/firmware/BatteryKit_BringUp)
   and **hold the button ≥ 1.5 s** (or send `a`): the sketch runs an automated
   self-test — reset reason, power-source sense, battery divider, INA180 zero,
   boost gating, a classified 50%-duty drive burst — and prints a PASS/FAIL
   report over serial. Then finish the two hands-on steps it lists: the `u`
   power-source test and the USB unplug/replug **handoff test** (the sketch
   counts source switches and reports the reset reason, so you get proof the
   handoff worked).
5. Install the MistMaker library (v2.1+) and try the examples — select the board with:

```cpp
MistMaker mist(MistMakerBatteryKitV041());  // V0.4 boards: same pins, same preset
                                            // V0.3 board? MistMakerBatteryKitV03()
```

## Notes

!!! warning "Li-Po safety"
    Only use protected 1S cells, never charge unattended, and keep the cell away from
    the water container. For workshops in venues that restrict lithium batteries, run
    this board from USB-C only — it works fine without a cell.

- Develop with the battery connected or not — the TPS2116 power mux always prefers
  USB when it's present, so serial enumeration and uploads just work (this fixes the
  power-sequence quirk of the [legacy V1.4 board](legacy-v1-4.md#known-quirks-fixes)).
- Piezo rail at power-on differs by revision: on **V0.4** it is live from power-on
  (pull-up on the boost enable) until firmware drives D3 — expected on a scope, and
  harmless: the gate driver holds the piezo off. On **V0.4.1** the rail (and LED2)
  stays **off** until firmware raises D3 — a dark board at plug-in is correct.
