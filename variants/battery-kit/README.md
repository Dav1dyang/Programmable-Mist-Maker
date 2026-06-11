# Battery Kit — V0.3

The portable, single-PCB "grab and go" Mist Maker: Li-Po battery + USB-C charging, full power path, piezo drive, current sensing, a button, a status LED — and (new in V0.3) **battery voltage monitoring** so the firmware can warn you when the pack is low and power down gracefully instead of browning out mid-mist.

Great for: installations, performances, kits, and anywhere without a USB cable.

## What's in this folder

| Subfolder | Contents |
|---|---|
| [`hardware/`](hardware/) | KiCad project, schematic/PCB PDFs, BOM, JLCPCB production files |
| [`firmware/BatteryKit_BringUp/`](firmware/BatteryKit_BringUp/) | Per-feature test sketch — flash this first on a new board |
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
| D8–D10 | — | Spare breakout |

## Key components ([full BOM](hardware/))

| Part | Role |
|---|---|
| LP4060B5F | Single-cell Li-Po USB-C charging |
| TPS2116DRLR | USB/battery power mux |
| AP7361C-3.3 | 3V3 LDO |
| TPS61023 | 3V→5.5V boost for the piezo rail |
| UCC27511A + DMT10H009LCG | Gate driver + MOSFET, 108.7 kHz switching |
| 3-legged tapped inductor (CD75) | LC voltage boost to ~30–40 Vpp |
| INA180A3 + 30 mΩ shunt | Analog current sense (3.0 V/A) |
| TS-1088 tactile switch, white LED | User button + status |
| Qwiic / JST-SH 4-pin | I2C expansion |

## Battery monitoring & graceful shutdown (V0.3)

D1 reads the pack through an equal-resistor divider (ratio 2.0). Guidance, measured under load:

| Voltage | Meaning | Firmware should |
|---|---|---|
| 4.2 V | Full | — |
| ~3.7 V | Nominal | — |
| < 3.45 V | Low | Warn (LED blink / UI banner) |
| < 3.20 V | Critical | Mist off → boost off → deep sleep |

The [MistMaker library](https://github.com/owochel/MistMaker) (v1.1+) wraps this: `batteryState()`, `batteryPercent()`, `shutdown()`. The `WiFiPhoneControl` and `HomeAssistant_MQTT` examples implement the full graceful power-off.

## Build your own

1. Order PCBs with the JLCPCB production files in [`hardware/`](hardware/) (SMT assembly recommended).
2. Solder the XIAO sockets, piezo connector (PH-2.0), and battery connector (PH-2.0).
3. Snap in a XIAO ESP32-C6, connect a 1S Li-Po (500 mAh+ recommended).
4. Flash [`firmware/BatteryKit_BringUp/`](firmware/BatteryKit_BringUp/) and walk its serial checklist (`h` for help). Check `b` (battery volts) with USB unplugged.
5. Install the MistMaker library and try the examples — select the board with:

```cpp
MistMaker mist(MistMakerBatteryKitV03());
```

## Notes

> [!WARNING]
> Li-Po safety: only use protected 1S cells, never charge unattended, and keep the cell away from the water container. For workshops in venues that restrict lithium batteries, run this board from USB-C only — it works fine without a cell.

- Always connect USB **before** battery when developing, or the serial port may not enumerate (see root README programming notes).
- Safety, cleaning, and known-issues notes live in the [root README](../../README.md).
