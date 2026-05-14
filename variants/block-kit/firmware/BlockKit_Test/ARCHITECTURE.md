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
                     │ IDLE (boot, LEDs visible)  │
                     │ strip = BREATH             │
                     │ deep-dim exp(sin) pulse;   │
                     │ exhale lingers at black    │
                     │ mist off                   │
                     └────┬───────────────────────┘
                          │ container docked (500 ms dwell)
                          ▼ (BREATH→WAVE crossfade, 1.1 s)
                 ┌─────────────────────────────────────────────┐
                 │ RUNNING                                     │
                 │   strip = WAVE: every LED always lit,       │
                 │   single broad gaussian swell traveling     │
                 │   bottom→top over 7.5 s (slow, meditative). │
                 │   mist on, modulated by the swell at the    │
                 │   piezo position (1 LED above index 0)      │
                 │   D7 off                                    │
                 └─────┬───────────────────────────────────────┘
                       │ container removed
                       ▼
                 ┌─────────────────────────────────────────────┐
                 │ TRANSITION_FROM_RUNNING                     │
                 │   mist hard-stopped                         │
                 │   wave still rendering, dims to 0 (~0.85 s) │
                 │   auto-enters IDLE →                        │
                 │   WAVE→BREATH mode SNAP (strip is dark) +   │
                 │   fast level restore (~0.64 s)              │
                 └─────┬───────────────────────────────────────┘
                       │ smoother reaches 0
                       ▼
                      IDLE

  Orthogonal short-press LED hide flag (any state):
     g_ledsHidden=false  ─── short-press ──►  g_ledsHidden=true
     LEDs visible (~640 ms fade)              LEDs dark, mist untouched
```

Short-press toggles `g_ledsHidden` in any state — the LED render fades to/from 0
over ~640 ms, but mist behavior is unaffected (so the diffuser keeps running at
the user-set level even while the visuals are blanked). Re-docking during
`TRANSITION_FROM_RUNNING` cleanly re-enters `RUNNING` (the crossfade engine
handles any mode swap mid-fade — no special case needed).

| From → To | Trigger | Effect |
|---|---|---|
| IDLE → RUNNING | reed Inserted (500 ms dwell) | mist on (wave-modulated), mode = WAVE → 1.1 s BREATH→WAVE crossfade in pre-gamma space |
| RUNNING → TRANSITION_FROM_RUNNING | reed Removed (100 ms dwell) | **mist hard-stop**; mode stays WAVE, baseLevel ramps 255→0 (~0.85 s) — wave dims naturally |
| TRANSITION_FROM_RUNNING → IDLE | smoother lands on 0 | mode→BREATH via `ledSetModeSnap` (no crossfade — strip is dark, so the snap is invisible). Fast fade-up to `g_userLevel` (~0.64 s) |
| TRANSITION_FROM_RUNNING → RUNNING | reed re-Inserted mid-fade | crossfade engine handles WAVE→WAVE no-op; mist re-armed |
| any state | button short-press | toggles `g_ledsHidden` (LED render fades to/from 0; mist + state untouched) |
| IDLE / RUNNING | button long-press | ramps `g_userLevel`; mist + LED brightness scale live (wave traverse rate stays constant) |

## Level Model (the trick)

One scalar drives both mist and LEDs so they always move together. A separate, orthogonal scaler hides the LED strip without touching the mist.

```
g_userLevel    (0..255)   ← user's intent; long-press ramps this
g_targetLevel  (0..255)   ← state-driven goal (0 in
                            TRANSITION_FROM_RUNNING, g_userLevel otherwise)
g_currentLevel (0..255)   ← smoothed actual; ramps toward target +2 / 10 ms
                            (≈ 1.3 s 0→255). Step size is intentionally tiny
                            so the slide reads as continuous, not stepped —
                            the prior +3/+4 stepping was the "snappy" feel.
                            Post-removal breath restore uses STEP_UP_FAST
                            (~0.64 s) so the cinematic stays brisk.

g_ledsHidden   (bool)     ← short-press toggle; orthogonal to state
g_ledScale     (0..255)   ← smoothed visibility scaler; eases toward
                            g_ledScaleTarget = (g_ledsHidden ? 0 : 255)
                            over ~640 ms (step 4 / 10 ms). Multiplies the
                            baseLevel handed to ledRender; mist ignores it.

mist duty   = (computeMistOutLevel(g_currentLevel, now) × MIST_DUTY_MAX) / 255
              where computeMistOutLevel(level, now) applies the wave-sync
              factor:
                factor (Q8) = MIST_WAVE_TROUGH_Q8
                            + ((256 - MIST_WAVE_TROUGH_Q8) × gauss_at_piezo) >> 8
                gauss_at_piezo = waveIntensityAtPiezo(now)  // 0..255
              In any state other than RUNNING, mist is inhibited (boost
              rail down) so the value is don't-care.
LED bright  = led_driver((g_currentLevel × g_ledScale) >> 8, g_ledMode)
              where led_driver:
                BREATH:  exp(sin) curve LUT, capped to LED_BREATH_PEAK (raw
                         80 → PWM 10/255 ≈ 4 %) so idle stays dim regardless
                         of baseLevel; all 14 LEDs share brightness;
                         baseLevel scales the whole.
                WAVE:    every LED at WAVE_BASE_LEVEL, plus gaussian swell
                         (σ=4 LEDs, 64-entry LUT, linear-interp) traveling
                         bottom→top over WAVE_PERIOD_MS. baseLevel scales
                         the whole render uniformly.
              Mode swaps (ledSetMode) auto-trigger a 1.1 s pre-gamma
              crossfade between the previous and current mode.
```

Pieces sitting outside the smoother:
- **Reed lift** → `mistHardStop()` cuts boost rail + PWM immediately and sets an inhibit flag so the still-fading `g_currentLevel` can't re-engage the boost. `enterRunning()` clears the inhibit.
- **Wave-mist sync** → in `RUNNING`, mist drive is *additionally* modulated by `waveIntensityAtPiezo(now)`, which samples the wave's gaussian 1 LED above the top of the strip (where the piezo sits). Same `now` is fed to the visible WAVE renderer, so the mist crest you feel is the wave crest you see, delayed only by the geometry of the piezo sitting above the top LED.
- **Short-press LED hide** → toggles `g_ledsHidden`. `g_ledScale` smooths in/out over ~640 ms and multiplies the baseLevel handed to `ledRender`. Mist drive does not pass through this scaler, so blanking the visuals never stops the diffuser.
- **D7** is driven once per loop from `containerIsPresent()` — independent of `state`, `level`, and `g_ledsHidden`.
- **led_driver crossfade engine** — `ledSetMode()` captures the prior mode and starts a `LED_CROSSFADE_MS` timer. While elapsed < timer, both modes render every frame and outputs are linearly blended per LED in pre-gamma space. Replaces the prior `g_pendingSwirl` state-machine flag — the BREATH↔WAVE swap is now a single continuous dissolve, not "fade up then snap".

## User Interactions

| User does | Result |
|---|---|
| Power on | Boots to `IDLE` with LEDs visible — strip starts a deep-dim exp(sin) breath; the exhale dwells at full black for a beat each cycle |
| Dock container | ~500 ms reed dwell → mist on (wave-modulated at the piezo); mode flips to WAVE, the dim breath dissolves into the slow gaussian swell over 1.1 s (single continuous crossfade). Mist visibly pulses in sync with the LED swell. |
| Lift container | Mist hard-stops instantly. Wave dims to black over ~0.85 s (mode stays WAVE so it reads as continuous). At black, mode SNAPS to BREATH (invisible since strip is dark — avoids a crossfade flash where WAVE's higher raw output would briefly dominate as baseLevel rose), then the dim breath fades in cleanly (~0.64 s) |
| Tap button (any state) | Toggles `g_ledsHidden` — LED strip fades to dark (or back in) over ~640 ms. **Mist is not affected** — if a container is docked, the diffuser keeps running at the user-set level even while the strip is hidden |
| Hold button (IDLE or RUNNING) | Ramps `g_userLevel` — brightness scales live and mist drive scales with it (still wave-modulated in RUNNING). Wave traverse rate stays constant. Direction alternates on release. The ramp clamps at 0 / 255 — there is no auto-snap-to-off; long-pressing to 0 just leaves the device at level 0 (mist off, LEDs dark), and the next long-press in the opposite direction brings it back. |

## Files

| File | Role |
|---|---|
| `BlockKit_Test.ino` | state machine, smoother, LED-visibility smoother, wave-mist sync (`computeMistOutLevel`), serial parser, glue |
| `pins.h` | pin defs, tunables, enums — single source of truth |
| `mist.ino` | `mistApply(level)`, `mistHardStop`, boost rail gating, inhibit |
| `led_driver.ino` | `ledRender(baseLevel)` — renders BREATH (exp(sin) LUT, linear-interp, capped to LED_BREATH_PEAK) or WAVE (gaussian-LUT swell traveling bottom→top), 1.1 s pre-gamma crossfade between modes, gamma + per-LED I2C write cache. Also exposes `waveIntensityAtPiezo(now)` so the main loop can phase-lock mist drive to the wave swell. |
| `status_led.ino` | D7 dim-on / off |
| `container.ino` | reed debounce, edge events |
| `button.ino` | debounce, short/long-press events |
| `current_sense.ino` | INA180 ADC @ 1 kHz, rolling mean+variance, scope mode |

Arduino concatenates all `.ino` files into one translation unit, so file-scope `static` only scopes per file by convention — names use module prefixes (`g_btnLastTickMs`, `g_ledLastRenderMs`, …) to avoid collisions.

## Serial Commands (115200 baud)

| Cmd | Effect |
|---|---|
| `help` | print list |
| `l` / `L` / `t` | hide LEDs / show LEDs / toggle visibility (mist untouched) |
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
| `MIST_PIEZO_OFFSET_LEDS_Q8` | 256 | Piezo position above index 0 (1 LED) for the wave-mist sync gauss sample |
| `MIST_WAVE_TROUGH_Q8` | 92 | Mist trough factor (92/256 ≈ 36 %) — matches `WAVE_BASE_LEVEL` / 255 so the mist's swing mirrors the LED's |
| `LEVEL_DEFAULT` | 255 | First-boot mist + brightness |
| `LEVEL_SMOOTH_TICK_MS` / `STEP_UP` / `STEP_DN` | 10 / 2 / 3 | ~1.3 s fade-in, ~0.85 s fade-out — tiny steps so the slide reads as continuous |
| `LEVEL_SMOOTH_STEP_UP_FAST` | 4 | ~0.64 s breath restore after removal — keeps the cinematic brisk |
| `LED_SCALE_STEP_PER_TICK` | 4 | ~0.64 s short-press LED hide/show fade |
| `LED_BREATH_PEAK` | 80 | raw cap on breath sweep → PWM 10/255 ≈ 4 % post-gamma. Picked so the sweep covers ~10 distinct PWM levels (smooth gradient) instead of the 1 level peak=38 gave (blink-like). |
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
