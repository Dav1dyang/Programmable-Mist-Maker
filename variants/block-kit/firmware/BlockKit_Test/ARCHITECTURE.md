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
                     ┌─────────────────────────────┐
                     │ IDLE_LEDS_ON (boot default) │
                     │ strip: dim breath, 10..45 % │
                     │ very dim & dramatic, 6.5 s  │
                     └────┬────────────────────────┘
                          │ short-press ▲ / ▼ short-press
                     ┌────▼─────────────────┐
                     │ IDLE_LEDS_OFF        │
                     │ strip dark           │
                     └──────────────────────┘

  IDLE_LEDS_ON / IDLE_LEDS_OFF
        │ container docked (500 ms reed dwell)
        ▼
  ┌────────────────────────────────────────────────────────────┐
  │ RUNNING                                                    │
  │   wave activation crossfades 0 → full over ~3 s:           │
  │   center brightness rises, breath fades out, slow drifting │
  │   wave fades in (single continuous motion — no mode flip)  │
  │   wave: one broad swell, bottom→top, 4.5 s per traversal   │
  │   mist on, D7 off                                          │
  └────┬───────────────────────────────────────────────────────┘
       │ container removed
       ▼
  ┌────────────────────────────────────────────────────────────┐
  │ TRANSITION_FROM_RUNNING                                    │
  │   mist hard-stopped                                        │
  │   envelope dims to 0 (~640 ms) preserving the wave shape   │
  │   auto-enters IDLE_LEDS_ON with fast breath restore (~425 ms) │
  └────┬───────────────────────────────────────────────────────┘
       │ base16 reaches 0
       ▼
       IDLE_LEDS_ON  (cinematic total ≈ 1 s)
```

Short-press from any active state → `IDLE_LEDS_OFF` (mute, skips cinematic).
Re-docking during `TRANSITION_FROM_RUNNING` cleanly re-enters `RUNNING`.

| From → To | Trigger | Effect |
|---|---|---|
| IDLE → RUNNING | reed Inserted (500 ms dwell) **or** button short-press + container docked | mist on; `g_waveActivation16` ramps 0→full over ~3 s — single crossfade |
| RUNNING → TRANSITION_FROM_RUNNING | reed Removed (100 ms dwell) | **mist hard-stop**; `g_baseLevel16` ramps to 0 over ~640 ms, wave shape preserved |
| TRANSITION_FROM_RUNNING → IDLE_LEDS_ON | smoother lands `base16` on 0 | `g_waveActivation16` snaps to 0 (invisible at dark); base ramps up at fast rate (~425 ms) |
| RUNNING / TRANSITION → IDLE_LEDS_OFF | button short-press | snap target both to 0 (skips cinematic) |
| IDLE_LEDS_OFF ↔ IDLE_LEDS_ON | button short-press (no container) | breath fades in/out via base16 ramp |
| RUNNING / IDLE_LEDS_ON | button long-press | ramps `g_userLevel`; base target follows live. Wave rate stays constant in RUNNING (dim is brightness-only) |
| any | level dims past `LEVEL_OFF_THRESHOLD` (8) | snap to `IDLE_LEDS_OFF`, reset `userLevel = LEVEL_DEFAULT`, flip ramp direction |

## Lighting Model (the trick)

**There is no LED mode.** One formula renders every frame; the visible look is a continuous interpolation between two endpoint envelopes ("idle" and "running") driven by two smoothed inputs.

```
For each LED i in [0..13]   (i=0 = top, i=13 = bottom):

  center    = lerp(IDLE_CENTER,    RUNNING_CENTER,    waveAct)  × base / 255
  breathAmp = lerp(IDLE_BREATH_AMP, 0,                waveAct)  × base / 255
  waveAmp   = lerp(0,               RUNNING_WAVE_AMP, waveAct)  × base / 255

  bright[i] = center
            + breathAmp · sin(2π · t / BREATH_PERIOD)
            + waveAmp   · sin(2π · t / WAVE_PERIOD + 2π · (13-i) / 14)

  pwm[i]    = GAMMA_LUT[clamp(bright[i], 0, 255)]
```

Two state-driven 16-bit smoothed inputs:

```
g_userLevel        (0..255)    ← user's intent; long-press ramps this
g_baseLevel16      (0..65535)  ← envelope amplitude. Target = g_userLevel×257
                                  in IDLE_LEDS_ON / RUNNING, 0 in IDLE_LEDS_OFF
                                  and TRANSITION_FROM_RUNNING. Also drives mist:
                                    mist_duty = (base8 × MIST_DUTY_MAX) / 255
g_waveActivation16 (0..65535)  ← 0 = pure breath (idle look)
                                  65535 = pure wave (running look)
                                  3 s ramp on dock/undock = single crossfade
```

Smoother math is fully 16-bit so per-tick increments are sub-8-bit: fractional bits accumulate between ticks so the visible 8-bit output advances smoothly even on short ramps. Mist consumes `base8 = base16 >> 8`; ledRender consumes both 8-bit views.

Both sine generators have **free-running phase** (`now % period`) so they NEVER restart at a state change — the lighting feels alive through every transition.

Per-state targets:

| State | base16 target | waveAct16 target | step rate |
|---|---|---|---|
| IDLE_LEDS_OFF | 0 | 0 | normal |
| IDLE_LEDS_ON | `userLevel × 257` | 0 | normal (fast on auto-promotion from TRANSITION) |
| RUNNING | `userLevel × 257` | 65535 | base normal, waveAct slow (3 s) |
| TRANSITION_FROM_RUNNING | 0 | *(unchanged — wave shape preserved)* | down rate |

Pieces sitting outside the smoother:
- **Reed lift** → `mistHardStop()` cuts boost rail + PWM immediately and sets an inhibit flag so the still-fading `g_baseLevel16` can't re-engage the boost. `enterRunning()` clears the inhibit.
- **D7** is driven once per loop from `containerIsPresent()` — independent of `state` and `level`.

## User Interactions

| User does | Result |
|---|---|
| Power on | Boots to `IDLE_LEDS_ON` — strip fades up from dark to a very dim "dramatic breath" (10 %..45 %, 6.5 s cycle) |
| Dock container | ~500 ms reed dwell → mist on; `g_waveActivation16` ramps 0→full over ~3 s. Brightness rises, breath fades out, slow drifting wave fades in — **one continuous motion** |
| Lift container | Mist hard-stops instantly. Envelope dims to 0 over ~640 ms with the wave shape preserved, then auto-restores the breath over ~425 ms (cinematic total ≈ 1 s) |
| Tap button (no container) | Toggles breath on / off |
| Tap button (any active state) | Snaps to `IDLE_LEDS_OFF` (skips the removal cinematic if mid-transition) |
| Tap button when muted, container docked | Resumes RUNNING (full crossfade replays) |
| Hold button (RUNNING or IDLE_LEDS_ON) | Ramps `g_userLevel` — base16 target follows live so the whole envelope dims uniformly. Wave rate stays constant. Direction alternates on release |
| Hold button past `LEVEL_OFF_THRESHOLD` (8) | Auto-snap to `IDLE_LEDS_OFF`, user level resets to default so the next wake comes back at full |

## Files

| File | Role |
|---|---|
| `BlockKit_Test.ino` | state machine, smoother, serial parser, glue |
| `pins.h` | pin defs, tunables, enums — single source of truth |
| `mist.ino` | `mistApply(level)`, `mistHardStop`, boost rail gating, inhibit |
| `led_driver.ino` | `ledRender(base8, waveAct8)` — unified continuous-modulation renderer: per-LED `center + breathAmp·sin(t) + waveAmp·sin(t+i)`, sine LUT + linear interp, gamma + per-LED I2C write cache |
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
| `pN` | breath period_ms 1000..20000 (default 6500) |
| `qN` | wave   period_ms 1000..20000 (default 4500) |
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
| `LEVEL_SMOOTH_TICK_MS` | 10 | Smoother tick rate |
| `LEVEL_BASE_STEP_UP_16` / `STEP_DN_16` | 770 / 1024 | ~850 ms fade-in / ~640 ms fade-out (16-bit step per tick) |
| `LEVEL_BASE_STEP_UP_FAST_16` | 1540 | ~425 ms post-removal breath restore — cinematic ≈ 1 s |
| `LEVEL_WAVE_ACT_STEP_16` | 219 | ~3 s dock/undock crossfade |
| `LEVEL_OFF_THRESHOLD` | 8 | Snap-to-off cutoff during dim ramp |
| `LED_IDLE_CENTER_MAX` / `_BREATH_AMP_MAX` | 70 / 44 | Idle envelope: ~10 %..45 % range after gamma |
| `LED_RUNNING_CENTER_MAX` / `_WAVE_AMP_MAX` | 185 / 60 | Running envelope: ~25 %..85 % range after gamma |
| `LED_BREATH_PERIOD_MS` | 6500 | Very dim & dramatic — slow inhale/exhale |
| `LED_WAVE_PERIOD_MS` | 4500 | Slow & meditative — one peak drifts bottom→top |
| `LED_TICK_MS` | 20 | 50 fps render |
| `REED_INSERT_DWELL_MS` / `REMOVE_DWELL_MS` | 500 / 100 | Asymmetric: slow on, fast off |
| `BUTTON_LONGPRESS_MS` / `LONGTICK_MS` | 500 / 13 | ~77 steps/sec while held |
| `STATUS_LED_DIM_DUTY` | 24 / 255 | ~10 % brightness — visible, not bright |

## What's NOT in Phase A

- Water-level classifier (variance-based LOW_WATER detection). Bench-validation data is collected via scope mode (`s`); Phase B PR adds the classifier.
- NVS persistence of `g_userLevel`. Reset every boot.
- Battery monitoring. Requires V0.2 hardware (VBAT divider).
- Watchdog (`esp_task_wdt`). Deferred so debugger pauses don't reset.
