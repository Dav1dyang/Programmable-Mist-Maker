# Battery Kit — V0.4.1

The portable, single-PCB "grab and go" Mist Maker: Li-Po battery + USB-C charging, full power path, piezo drive, current sensing, a button, a status LED — and **trustworthy battery monitoring**: since V0.4 the board routes the power mux's status pin to the XIAO (D8), so firmware knows whether it is running from USB or the cell and low-battery logic can never false-trigger while plugged in.

> **V0.4.1** is the July 2026 production revision (the boards in the shop): R22 → 220 kΩ widens the D8 USB-detect margin, R8 → pull-down means the ~5 V rail boots **off** (and deep sleep drops to ~0.25 mA), R7 → 5.1 kΩ dims LED2. Same pin map as V0.4. The [`hardware/`](hardware/) folder holds the as-fabbed V0.4.1 KiCad + production files, the design-review memos, and the acceptance-test protocol.

Great for: installations, performances, kits, and anywhere without a USB cable.

## What's in this folder

| Subfolder | Contents |
|---|---|
| [`hardware/`](hardware/) | As-fabbed V0.4.1 KiCad project, schematic PDF, BOM, JLCPCB production files, design reviews + acceptance protocol |
| [`firmware/BatteryKit_BringUp/`](firmware/BatteryKit_BringUp/) | Bring-up sketch — flash first; hold the button ≥ 1.5 s for the **automated self-test** |
| [`firmware/DutySweep_Test/`](firmware/DutySweep_Test/) | Duty→current characterization tool (the data behind the 50% default) |
| `enclosure/` | 3D-printable demo enclosure (coming — see [root README](../../README.md#enclosures)) |

## How it works

```
USB-C ─► LP4060 charger ─► Li-Po ─┐
                                  ├─► TPS2116 power mux ─► AP7361C 3V3 LDO ─► XIAO
USB-C 5V ─────────────────────────┘
                                  └─► TPS61023 boost (EN = D3) ─► 5V5 rail
5V5 ─► UCC27511 ─► DMT10H009 MOSFET ─► 3-leg inductor ─► piezo disc
        ▲ D0: 108.7 kHz PWM
5V5 current ─► 30 mΩ shunt ─► INA180A3 ─► D2 (analog)
VBAT ─► ½ divider ─► D1 (analog)
```

- **Power path:** the TPS2116 mux picks USB when present, battery otherwise; the LP4060 charges the Li-Po from USB-C.
- **Piezo rail:** the TPS61023 boost (enabled by D3) makes a stable 5.5 V from either source. Turn it off when idle — it's the single biggest standby drain.
- **Sensing:** INA180A3 + 30 mΩ shunt → analog D2 for disc/water detection; battery divider → D1 for the fuel gauge.

## Pin map

| XIAO pin | Net | Function |
|---|---|---|
| D0 | `MIST_PWM_3V3` | 108.7 kHz PWM to gate driver |
| D1 | `D1_BATT_VOLTAGE` | Battery voltage via ½ divider (V0.3+) |
| D2 | `D2_CS` | INA180A3 current-sense output |
| D3 | `D3_TPS_EN` | TPS61023 boost enable (HIGH = 5V5 on) |
| D4 / D5 | `D4_SDA` / `D5_SCL` | I2C + Qwiic connector |
| D6 | `D6_BUTTON` | Button, active HIGH (10k pull-down on PCB) |
| D7 | `D7_LED` | Status LED |
| D8 | `D8_ST` | **Power-mux status (V0.4+):** HIGH = on USB, LOW = on the cell — plain `INPUT`, no internal pull |
| D9–D10 | — | Spare breakout |

## Key components ([full BOM](hardware/))

| Part | Role |
|---|---|
| LP4060B5F | Single-cell Li-Po USB-C charging |
| TPS2116DRLR | USB/battery power mux |
| AP7361C-3.3 | 3V3 LDO |
| TPS61023 | 3V→5.5V boost for the piezo rail |
| UCC27511A + DMT10H009LCG | Gate driver + MOSFET, 108.7 kHz switching |
| 3-legged tapped inductor (CD75) | LC voltage boost to ~80 Vpp |
| INA180A3 + 30 mΩ shunt | Analog current sense (3.0 V/A) |
| TS-1088 tactile switch, white LED | User button + status |
| Qwiic / JST-SH 4-pin | I2C expansion |

## Battery monitoring & graceful shutdown

D1 reads the pack through an equal-resistor divider (ratio 2.0). On USB the node tracks the *charger*, not state-of-charge — which is why V0.4 added the D8 source sense, and why on a V0.3 board you should call `disableBattery()`. Guidance, measured under load (valid on the cell):

| Voltage | Meaning | Firmware should |
|---|---|---|
| 4.2 V | Full | — |
| ~3.7 V | Nominal | — |
| < 3.45 V | Low | Warn (LED blink / UI banner) |
| < 3.20 V | Critical | Mist off → boost off → deep sleep |

The [MistMaker library](https://github.com/owochel/MistMaker) (v2.1+) wraps this and gates it on the D8 source sense — `batteryState()` returns `CHARGING` on USB and only ever `LOW`/`CRITICAL` on the cell: `batteryState()`, `batteryPercent()`, `usbPresent()`, `shutdown()`. The `WiFiPhoneControl` and `HomeAssistant_MQTT` examples implement the full graceful power-off.

## Build your own

1. Order PCBs with the JLCPCB production files in [`hardware/`](hardware/) (SMT assembly recommended).
2. Solder the XIAO sockets, piezo connector (PH-2.0), and battery connector (PH-2.0).
3. Snap in a XIAO ESP32-C6, connect a 1S Li-Po (500 mAh+ recommended).
4. Flash [`firmware/BatteryKit_BringUp/`](firmware/BatteryKit_BringUp/) and hold the button ≥ 1.5 s (or send `a`): the **automated self-test** checks reset reason, power-source sense, battery divider, current-sense zero, boost gating, and a classified drive burst, then prints a PASS/FAIL report. Finish with the manual steps it lists (`u` power-source test, USB unplug/replug handoff).
5. Install the MistMaker library (v2.1+) and try the examples — select the board with:

```cpp
MistMaker mist(MistMakerBatteryKitV041());  // V0.4: same preset; V0.3: MistMakerBatteryKitV03()
```

## Notes

> [!WARNING]
> Li-Po safety: only use protected 1S cells, never charge unattended, and keep the cell away from the water container. For workshops in venues that restrict lithium batteries, run this board from USB-C only — it works fine without a cell.

- Develop with the battery connected or not — the TPS2116 power mux always prefers USB when present, so serial enumeration and uploads just work (this fixes the legacy V1.4 power-sequence quirk).
- Safety, cleaning, and known-issues notes live in the [root README](../../README.md).
