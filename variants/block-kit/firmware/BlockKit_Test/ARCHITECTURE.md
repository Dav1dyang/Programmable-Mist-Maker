# Block Kit V0.1 — Phase A Firmware at a Glance

One-page technical overview. For install + bring-up steps see [`README.md`](README.md).

## Stack

| Layer | Choice |
|---|---|
| MCU | Seeed XIAO ESP32-C6 (RISC-V, 160 MHz, 512 KB SRAM, 4 MB flash) |
| Toolchain | Arduino IDE 2.x + `esp32` core v3.3+ |
| Sketch layout | Multi-`.ino` (Arduino auto-concatenates) — no `.cpp`/`.h` pairs |
| External lib | [`Dav1dyang/Adafruit_IS31FL3731`](https://github.com/Dav1dyang/Adafruit_IS31FL3731) fork (Matrix B addressing) |
| State, allocation | One state machine, all `static` module-private state, **no heap, no `String`** |
| Math style | Integer + LUTs (no `sinf()` — ESP32-C6 has no FPU) |
| Boards supported | XIAO_ESP32C6 (production), XIAO_ESP32S3 (port), QT Py ESP32 family (compile-only) |

## Hardware (3 PCBs)

| PCB | Role |
|---|---|
| **Driver** | XIAO socket, USB-C + LP4068BF charger, TPS2116 mux, AP7361C 3V3 LDO, TPS61023 3V3→5V boost, UCC27518 level shifter, DMT10H009 MOSFET → piezo, INA180A3 + 30 mΩ shunt |
| **Mist & LED** | IS31FL3731 (14 LEDs on Matrix B / CB1+CB2), reed switch, piezo connector, Qwiic |
| **Button** | Single B3AL-1002P tactile on WAGO-2059 |

Mist drive: 108.7 kHz / 0–50 % PWM into a piezo disc. Container has a magnet that closes the reed when docked.

## Pin Map (XIAO ESP32-C6)

| Net | Pin | GPIO | Direction | Notes |
|---|---|---|---|---|
| `PIN_MIST_PWM` | D0 | 0 | OUT (LEDC) | 108.7 kHz, duty 0..127 |
| `PIN_CURRENT_ADC` | D2 | 2 | ADC1_CH2 | INA180A3 via 1 kΩ + 1 µF LPF |
| `PIN_BOOST_EN` | D3 | 21 | OUT | TPS61023 EN (HIGH = 5 V rail on); side-effect: red LED2 lights |
| `PIN_BUTTON` | D6 | 16 | IN_PULLDOWN | Active-HIGH; PCB 10 kΩ pull-down |
| `PIN_STATUS_LED` | D7 | 17 | OUT (LEDC) | White LED, dim "waiting" indicator |
| `PIN_REED` | D10 | 18 | IN_PULLUP | LOW = magnet present; **V0.1 blue-wire rework** |
| SDA / SCL | D4 / D5 | 22 / 23 | I2C | IS31FL3731 (0x74) + Qwiic |

`D0..D10` come from the XIAO platform header directly. Non-XIAO boards (QT Py) get explicit per-variant `PIN_*` defs guarded by `ARDUINO_xxx` macros; unknown boards `#error`.

## State Machine

```
                     ┌─────────────────────┐
                     │ IDLE_LEDS_ON (boot) │
                     │ strip = BREATH      │
                     │ uniform soft pulse  │
                     └────┬────────────────┘
                          │ short-press ▲ / ▼ short-press
                     ┌────▼────────────────┐
                     │ IDLE_LEDS_OFF       │
                     │ strip dark          │
                     └─────────────────────┘

  IDLE_LEDS_ON / IDLE_LEDS_OFF
        │ container docked (500 ms dwell)
        ▼
  ┌─────────────────────────────────────────┐
  │ RUNNING                                 │
  │   1) strip = BREATH while fading up     │
  │   2) at peak, strip = SWIRL (rising ↑)  │
  │   mist on, D7 off                       │
  └─────┬───────────────────────────────────┘
        │ container removed
        ▼
  ┌─────────────────────────────────────────┐
  │ TRANSITION_FROM_RUNNING                 │
  │   mist hard-stopped                     │
  │   swirl decelerates & dims (~640 ms)    │
  │   auto-enters IDLE_LEDS_ON →            │
  │   fast breath fade-up (~425 ms)         │
  └─────┬───────────────────────────────────┘
        │ smoother reaches 0
        ▼
       IDLE_LEDS_ON  (cinematic total ≈ 1 s)
```

Short-press from any active state → `IDLE_LEDS_OFF` (mute, skips cinematic).
Re-docking during `TRANSITION_FROM_RUNNING` cleanly re-enters `RUNNING`.

| From → To | Trigger | Effect |
|---|---|---|
| IDLE → RUNNING | reed Inserted (500 ms dwell) **or** button short-press + container docked | mist on, strip fades up in BREATH, **then** flips to SWIRL at peak |
| RUNNING → TRANSITION_FROM_RUNNING | reed Removed (100 ms dwell) | **mist hard-stop**; swirl decelerates + dims over ~640 ms |
| TRANSITION_FROM_RUNNING → IDLE_LEDS_ON | smoother lands on 0 | mode→BREATH, fast fade-up to `g_userLevel` (~425 ms) |
| RUNNING / TRANSITION → IDLE_LEDS_OFF | button short-press | snap to mute (skips cinematic) |
| IDLE_LEDS_OFF ↔ IDLE_LEDS_ON | button short-press (no container) | breath fades in/out |
| RUNNING / IDLE_LEDS_ON | button long-press | ramps `g_userLevel`; brightness scales live (chase speed stays constant in RUNNING) |
| any | level dims past `LEVEL_OFF_THRESHOLD` (8) | snap to `IDLE_LEDS_OFF`, reset `userLevel = LEVEL_DEFAULT`, flip ramp direction |

## Level Model (the trick)

One scalar drives both mist and LEDs so they always move together.

```
g_userLevel    (0..255)   ← user's intent; long-press ramps this
g_targetLevel  (0..255)   ← state-driven goal (0 in IDLE_LEDS_OFF /
                            TRANSITION_FROM_RUNNING, g_userLevel otherwise)
g_currentLevel (0..255)   ← smoothed actual; ramps toward target ~3 / 10 ms
                            (≈ 850 ms 0→255, "luxurious"). Post-removal
                            breath restore uses STEP_UP_FAST (~425 ms) so
                            the full cinematic lands ≈ 1 s.

mist duty   = (g_currentLevel × MIST_DUTY_MAX) / 255    // 127 = 50 %
LED bright  = led_driver(g_currentLevel, g_ledMode)
              where:
                BREATH:  uniform + sineLUT(t) × depth × baseLevel/255
                SWIRL:   per-LED 0..255 from distance behind rising head,
                         × baseLevel/255, gamma-corrected. Phase advance
                         scales with baseLevel iff ledSetSwirlFading(true)
                         (set during TRANSITION_FROM_RUNNING).
```

Pieces sitting outside the smoother:
- **Reed lift** → `mistHardStop()` cuts boost rail + PWM immediately and sets an inhibit flag so the still-fading `g_currentLevel` can't re-engage the boost. `enterRunning()` clears the inhibit.
- **D7** is driven once per loop from `containerIsPresent()` — independent of `state` and `level`.
- **`g_pendingSwirl`** — when `enterRunning()` fires, the strip starts in BREATH and the smoother flips it to SWIRL on landing at target. Makes "brighten up, *then* swirl" sequential.

## User Interactions

| User does | Result |
|---|---|
| Power on | Boots to `IDLE_LEDS_ON` — strip starts a soft uniform breath at full user level |
| Dock container | ~500 ms reed dwell → mist on; strip fades up in BREATH, **then** flips to SWIRL (rising chase) at peak |
| Lift container | Mist hard-stops instantly. Swirl decelerates + dims ~640 ms, then auto-restores the BREATH at user level over ~425 ms (cinematic total ≈ 1 s) |
| Tap button (no container) | Toggles BREATH on / off |
| Tap button (any active state) | Snaps to `IDLE_LEDS_OFF` (skips the removal cinematic if mid-transition) |
| Tap button when muted, container docked | Resumes RUNNING (fade-up → swirl) |
| Hold button (RUNNING or IDLE_LEDS_ON) | Ramps `g_userLevel` — brightness scales live. Swirl rotation speed stays constant in RUNNING (dim is brightness-only). Direction alternates on release |
| Hold button past `LEVEL_OFF_THRESHOLD` (8) | Auto-snap to `IDLE_LEDS_OFF`, user level resets to default so the next wake comes back at full |

## Files

| File | Role |
|---|---|
| `BlockKit_Test.ino` | state machine, smoother, serial parser, glue |
| `pins.h` | pin defs, tunables, enums — single source of truth |
| `mist.ino` | `mistApply(level)`, `mistHardStop`, boost rail gating, inhibit |
| `led_driver.ino` | `ledRender(baseLevel)` — dispatches BREATH (sine LUT + linear interp) or SWIRL (Q8 phase + per-LED chase), gamma + per-LED I2C write cache |
| `status_led.ino` | D7 dim-on / off |
| `container.ino` | reed debounce, edge events |
| `button.ino` | debounce, short/long-press events |
| `current_sense.ino` | INA180 ADC @ 1 kHz, rolling mean+variance, scope mode |

Arduino concatenates all `.ino` files into one translation unit, so file-scope `static` only scopes per file by convention — names use module prefixes (`g_btnLastTickMs`, `g_ledLastRenderMs`, …) to avoid collisions.

## Serial Commands (115200 baud)

| Cmd | Effect |
|---|---|
| `help` | print list |
| `1` / `0` / `t` | mist on / off / toggle |
| `vN` | set `g_userLevel` 0..255 |
| `a0` / `a1` | LED breathing off / on |
| `dN` | breath depth 0..64 (default 16) |
| `pN` | breath period_ms 1000..20000 (default 4000) |
| `w` | LED walk (sequence 14 LEDs, ~14 s, blocks) |
| `r` | dump reed state (raw + debounced) |
| `s` | toggle scope mode (raw current samples @ 100 Hz) |
| `m` | mute / unmute periodic `[PLOT]` |
| `k` | (Phase B placeholder) recalibrate baseline |

Log line prefixes: `[APP]`, `[MIST]`, `[LED]`, `[REED]`, `[BTN]`, `[CUR]`, `[STAT]`, `[PLOT]`, `[CMD]`.

## Key Tuning Constants (`pins.h`)

| Constant | Value | Why |
|---|---|---|
| `MIST_FREQ_HZ` | 108 700 | Piezo disc resonance |
| `MIST_DUTY_MAX` | 127 | 50 % of 8-bit PWM = full mist |
| `LEVEL_DEFAULT` | 255 | First-boot mist + brightness |
| `LEVEL_SMOOTH_TICK_MS` / `STEP_UP` / `STEP_DN` | 10 / 3 / 4 | ~850 ms fade-in, ~640 ms fade-out |
| `LEVEL_SMOOTH_STEP_UP_FAST` | 6 | ~425 ms breath restore after removal — keeps the cinematic ≈ 1 s |
| `LEVEL_OFF_THRESHOLD` | 8 | Snap-to-off cutoff during dim ramp |
| `LED_BREATH_DEPTH` | 16 | ±~6 % modulation — subtle |
| `LED_BREATH_PERIOD_MS` | 4000 | One inhale/exhale |
| `SWIRL_PERIOD_MS` | 1500 | Head traverses 14 LEDs bottom→top |
| `SWIRL_TAIL_LEDS` | 6 | Fading tail length behind the head |
| `LED_TICK_MS` | 20 | 50 fps render |
| `REED_INSERT_DWELL_MS` / `REMOVE_DWELL_MS` | 500 / 100 | Asymmetric: slow on, fast off |
| `BUTTON_LONGPRESS_MS` / `LONGTICK_MS` | 500 / 13 | ~77 steps/sec while held |
| `STATUS_LED_DIM_DUTY` | 24 / 255 | ~10 % brightness — visible, not bright |

## What's NOT in Phase A

- Water-level classifier (variance-based LOW_WATER detection). Bench-validation data is collected via scope mode (`s`); Phase B PR adds the classifier.
- NVS persistence of `g_userLevel`. Reset every boot.
- Battery monitoring. Requires V0.2 hardware (VBAT divider).
- Watchdog (`esp_task_wdt`). Deferred so debugger pauses don't reset.
