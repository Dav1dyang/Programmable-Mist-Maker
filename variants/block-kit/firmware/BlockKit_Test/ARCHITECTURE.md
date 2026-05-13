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
                     ┌────────────────────────────┐
                     │ IDLE_LEDS_ON (boot)        │
                     │ strip = BREATH             │
                     │ deep-dim exp(sin) pulse;   │
                     │ exhale lingers at black    │
                     └────┬───────────────────────┘
                          │ short-press ▲ / ▼ short-press
                     ┌────▼────────────────┐
                     │ IDLE_LEDS_OFF       │
                     │ strip dark          │
                     └─────────────────────┘

  IDLE_LEDS_ON / IDLE_LEDS_OFF
        │ container docked (500 ms dwell)
        ▼ (BREATH→WAVE crossfade, 1.1 s)
  ┌─────────────────────────────────────────────┐
  │ RUNNING                                     │
  │   strip = WAVE: every LED always lit,       │
  │   single broad gaussian swell traveling     │
  │   bottom→top over 7.5 s (slow, meditative). │
  │   mist on, D7 off                           │
  └─────┬───────────────────────────────────────┘
        │ container removed
        ▼
  ┌─────────────────────────────────────────────┐
  │ TRANSITION_FROM_RUNNING                     │
  │   mist hard-stopped                         │
  │   wave still rendering, dims to 0 (~0.85 s) │
  │   auto-enters IDLE_LEDS_ON →                │
  │   WAVE→BREATH crossfade (1.1 s) +           │
  │   fast level restore (~0.64 s)              │
  └─────┬───────────────────────────────────────┘
        │ smoother reaches 0
        ▼
       IDLE_LEDS_ON
```

Short-press from any active state → `IDLE_LEDS_OFF` (mute, skips cinematic).
Re-docking during `TRANSITION_FROM_RUNNING` cleanly re-enters `RUNNING` (the
crossfade engine handles any mode swap mid-fade — no special case needed).

| From → To | Trigger | Effect |
|---|---|---|
| IDLE → RUNNING | reed Inserted (500 ms dwell) **or** button short-press + container docked | mist on, mode = WAVE → 1.1 s BREATH→WAVE crossfade in pre-gamma space |
| RUNNING → TRANSITION_FROM_RUNNING | reed Removed (100 ms dwell) | **mist hard-stop**; mode stays WAVE, baseLevel ramps 255→0 (~0.85 s) — wave dims naturally |
| TRANSITION_FROM_RUNNING → IDLE_LEDS_ON | smoother lands on 0 | mode→BREATH (kicks off WAVE→BREATH crossfade), fast fade-up to `g_userLevel` (~0.64 s) |
| RUNNING / TRANSITION → IDLE_LEDS_OFF | button short-press | snap to mute (skips cinematic) |
| IDLE_LEDS_OFF ↔ IDLE_LEDS_ON | button short-press (no container) | dim breath fades in/out (mode stays BREATH; no crossfade) |
| RUNNING / IDLE_LEDS_ON | button long-press | ramps `g_userLevel`; brightness scales live (wave traverse rate stays constant) |
| any | level dims past `LEVEL_OFF_THRESHOLD` (8) | snap to `IDLE_LEDS_OFF`, reset `userLevel = LEVEL_DEFAULT`, flip ramp direction |

## Level Model (the trick)

One scalar drives both mist and LEDs so they always move together.

```
g_userLevel    (0..255)   ← user's intent; long-press ramps this
g_targetLevel  (0..255)   ← state-driven goal (0 in IDLE_LEDS_OFF /
                            TRANSITION_FROM_RUNNING, g_userLevel otherwise)
g_currentLevel (0..255)   ← smoothed actual; ramps toward target +2 / 10 ms
                            (≈ 1.3 s 0→255). Step size is intentionally tiny
                            so the slide reads as continuous, not stepped —
                            the prior +3/+4 stepping was the "snappy" feel.
                            Post-removal breath restore uses STEP_UP_FAST
                            (~0.64 s) so the cinematic stays brisk.

mist duty   = (g_currentLevel × MIST_DUTY_MAX) / 255    // 127 = 50 %
LED bright  = led_driver(g_currentLevel, g_ledMode)
              where led_driver:
                BREATH:  exp(sin) curve LUT, capped to LED_BREATH_PEAK (≈15 %)
                         so idle stays dim regardless of baseLevel; all 14
                         LEDs share brightness; baseLevel scales the whole.
                WAVE:    every LED at WAVE_BASE_LEVEL, plus gaussian swell
                         (σ=4 LEDs, 64-entry LUT, linear-interp) traveling
                         bottom→top over WAVE_PERIOD_MS. baseLevel scales
                         the whole render uniformly.
              Mode swaps (ledSetMode) auto-trigger a 1.1 s pre-gamma
              crossfade between the previous and current mode.
```

Pieces sitting outside the smoother:
- **Reed lift** → `mistHardStop()` cuts boost rail + PWM immediately and sets an inhibit flag so the still-fading `g_currentLevel` can't re-engage the boost. `enterRunning()` clears the inhibit.
- **D7** is driven once per loop from `containerIsPresent()` — independent of `state` and `level`.
- **led_driver crossfade engine** — `ledSetMode()` captures the prior mode and starts a `LED_CROSSFADE_MS` timer. While elapsed < timer, both modes render every frame and outputs are linearly blended per LED in pre-gamma space. Replaces the prior `g_pendingSwirl` state-machine flag — the BREATH↔WAVE swap is now a single continuous dissolve, not "fade up then snap".

## User Interactions

| User does | Result |
|---|---|
| Power on | Boots to `IDLE_LEDS_ON` — strip starts a deep-dim exp(sin) breath; the exhale dwells at full black for a beat each cycle |
| Dock container | ~500 ms reed dwell → mist on; mode flips to WAVE, the dim breath dissolves into the slow gaussian swell over 1.1 s (single continuous crossfade) |
| Lift container | Mist hard-stops instantly. Wave dims to black over ~0.85 s (mode stays WAVE so it reads as continuous), then the dim breath crossfades back in over 1.1 s |
| Tap button (no container) | Toggles deep-dim BREATH on / off |
| Tap button (any active state) | Snaps to `IDLE_LEDS_OFF` (skips the removal cinematic if mid-transition) |
| Tap button when muted, container docked | Resumes RUNNING (BREATH→WAVE crossfade from black) |
| Hold button (RUNNING or IDLE_LEDS_ON) | Ramps `g_userLevel` — brightness scales live. Wave traverse rate stays constant in RUNNING (dim is brightness-only). Direction alternates on release |
| Hold button past `LEVEL_OFF_THRESHOLD` (8) | Auto-snap to `IDLE_LEDS_OFF`, user level resets to default so the next wake comes back at full |

## Files

| File | Role |
|---|---|
| `BlockKit_Test.ino` | state machine, smoother, serial parser, glue |
| `pins.h` | pin defs, tunables, enums — single source of truth |
| `mist.ino` | `mistApply(level)`, `mistHardStop`, boost rail gating, inhibit |
| `led_driver.ino` | `ledRender(baseLevel)` — renders BREATH (exp(sin) LUT, linear-interp, capped to LED_BREATH_PEAK) or WAVE (gaussian-LUT swell traveling bottom→top), 1.1 s pre-gamma crossfade between modes, gamma + per-LED I2C write cache |
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
| `LEVEL_SMOOTH_TICK_MS` / `STEP_UP` / `STEP_DN` | 10 / 2 / 3 | ~1.3 s fade-in, ~0.85 s fade-out — tiny steps so the slide reads as continuous |
| `LEVEL_SMOOTH_STEP_UP_FAST` | 4 | ~0.64 s breath restore after removal — keeps the cinematic brisk |
| `LEVEL_OFF_THRESHOLD` | 8 | Snap-to-off cutoff during dim ramp |
| `LED_BREATH_PEAK` | 38 | ~15 % post-gamma — *very dim*, dramatic peak (idle) |
| `LED_BREATH_PERIOD_MS` | 5500 | One slow inhale → exhale → black-dwell |
| `WAVE_BASE_LEVEL` | 92 | Always-on baseline every LED holds (docked) |
| `WAVE_SWELL_PEAK` | 163 | base + peak = 255 at swell crest |
| `WAVE_PERIOD_MS` | 7500 | One swell traverses the strip — slow, meditative |
| `WAVE_SIGMA_LEDS_Q8` | 1024 | σ = 4 LEDs — broad, soft gaussian (no hard edges) |
| `LED_CROSSFADE_MS` | 1100 | BREATH ↔ WAVE pre-gamma crossfade window |
| `LED_TICK_MS` | 20 | 50 fps render |
| `REED_INSERT_DWELL_MS` / `REMOVE_DWELL_MS` | 500 / 100 | Asymmetric: slow on, fast off |
| `BUTTON_LONGPRESS_MS` / `LONGTICK_MS` | 500 / 13 | ~77 steps/sec while held |
| `STATUS_LED_DIM_DUTY` | 24 / 255 | ~10 % brightness — visible, not bright |

## What's NOT in Phase A

- Water-level classifier (variance-based LOW_WATER detection). Bench-validation data is collected via scope mode (`s`); Phase B PR adds the classifier.
- NVS persistence of `g_userLevel`. Reset every boot.
- Battery monitoring. Requires V0.2 hardware (VBAT divider).
- Watchdog (`esp_task_wdt`). Deferred so debugger pauses don't reset.
