# BlockKit_Test — Phase A bring-up firmware

Block Kit V0.1 firmware that exercises every subsystem (mist PWM, reed switch, button, IS31FL3731 LED ring, D7 indicator LED) **plus** a "scope mode" that streams the INA180 current data so the water-detection algorithm can be designed from real bench data later (Phase B).

Phase A intentionally has **no water-level classifier**. The reed switch is the primary on-gesture (magnetic-on); the button toggles state and ramps level. One unified "level" variable drives both mist PWM duty and LED ring brightness — they always move together.

## States

Three top-level states, all transitions handled in `BlockKit_Test.ino::loop()`. A level smoother fades `g_currentLevel` toward whatever the new state's target is, so every transition is a soft ~800 ms fade rather than a snap.

| State            | When                          | Mist        | LED ring                | D7 white       |
|------------------|-------------------------------|-------------|-------------------------|----------------|
| `IDLE_LEDS_OFF`  | No container docked, default  | off         | dark                    | dim (~10 %)    |
| `IDLE_LEDS_ON`   | No container, LEDs toggled on | off         | breathing at user level | dim (~10 %)    |
| `RUNNING`        | Container docked              | PWM at level | breathing at user level | off            |

### Transition map

| From state       | Event                                | To state         | Notes                                       |
|------------------|--------------------------------------|------------------|---------------------------------------------|
| `IDLE_LEDS_OFF`  | Container inserted (reed 500 ms)     | `RUNNING`        | Mist + LEDs ramp up over ~800 ms            |
| `IDLE_LEDS_OFF`  | Button short-press (no container)    | `IDLE_LEDS_ON`   | LEDs fade in to user level                  |
| `IDLE_LEDS_ON`   | Container inserted (reed 500 ms)     | `RUNNING`        | Mist engages immediately at current level   |
| `IDLE_LEDS_ON`   | Button short-press                   | `IDLE_LEDS_OFF`  | LEDs fade out                               |
| `RUNNING`        | Container removed (reed 100 ms)      | `IDLE_LEDS_OFF`  | **Mist hard-stops** (safety); LEDs fade out |
| `RUNNING`        | Button short-press                   | `IDLE_LEDS_OFF`  | Mist hard-stops; LEDs fade out              |
| `RUNNING` or `IDLE_LEDS_ON` | Button long-press         | (no transition)  | Ramps `g_userLevel`; mist + LEDs follow live |
| any state where level dims past 8 | (auto)              | `IDLE_LEDS_OFF`  | Avoids "stuck dim" — user can re-engage     |

### One level to rule them all

`g_userLevel` (0..255) is the user-set "intensity". It controls:
- **Mist PWM duty** = `(level × MIST_DUTY_MAX) / 255` — level 255 ⇒ 50 % duty (full mist).
- **LED ring brightness** — base brightness directly, breath modulation layered on top.

`g_targetLevel` is what the current state wants the level to be (0 for IDLE_LEDS_OFF, `g_userLevel` otherwise). `g_currentLevel` is exponentially smoothed toward target each tick — that's the actual value applied to mist + LEDs. Both move together, fading luxuriously on state changes.

### Lift-off is a SAFETY event

When the reed opens (container lifted), mist is hard-stopped immediately — boost rail down, PWM 0, the lot. The LED ring continues fading via the smoother, which is purely visual. The reason: no surprise misting after the bottle is physically gone.

## Hardware required

- Block Kit V0.1 (3 PCBs assembled — Driver, Mist & LED, Button)
- Seeed XIAO ESP32-C6
- Piezo mist disc (108.7 kHz)
- Reed switch + magnet on the container module
- USB-C cable for power and serial

V0.1 needs the **reed-to-D10 blue-wire rework** described in `../../hardware/README.md`. Without it, the reed switch will not work.

## Install

1. Arduino IDE 2.x with the **esp32 by Espressif Systems** board package (v3.x).
2. Clone David's IS31FL3731 fork into your Arduino libraries folder:

   ```bash
   cd ~/Documents/Arduino/libraries
   git clone https://github.com/Dav1dyang/Adafruit_IS31FL3731
   ```

   Adafruit_BusIO and Adafruit_GFX (its transitive dependencies) install fine from Library Manager.
3. Open `BlockKit_Test.ino`. Select board **XIAO_ESP32C6**, USB CDC On Boot **Enabled**.
4. **Verify** to compile, then **Upload**.

## Serial console (115200 baud)

On boot you'll see the banner, the resolved pin map, and the command list. Every log line is tagged so you can grep one subsystem:

```
[APP] Block Kit V0.1 bring-up (Phase A)
[APP] PIN_MIST_PWM=D0 (GPIO 0)
[APP] PIN_REED=D10 (GPIO 18)
[APP] -> IDLE_LEDS_OFF
[REED] inserted
[APP] -> RUNNING
[MIST] on (boost up)
[STAT] state=RUNNING reed=1 btn=0 user=255 cur=140 mist=1 mean_mA=178.2
[PLOT] 178.4,12.3,2
[BTN] long-press start, dir=-1
```

- `[STAT]` prints once a second — a human-readable subsystem snapshot.
- `[PLOT]` is the Arduino Serial Plotter–friendly CSV: `mean_mA, var_mA2, state` at 20 Hz when scope mode is off, or `raw_mA, mean_mA, var_mA2` at 100 Hz when scope mode is on.
- Send `m` to mute `[PLOT]` while you read other lines.

### Commands

| Cmd | Effect |
|---|---|
| `help` | Print this list |
| `1` / `0` / `t` | Mist on / off / toggle (requires container for on) |
| `vN` | Set `g_userLevel` directly, 0..255 (e.g. `v180`) |
| `a0` / `a1` | LED breathing animation off / on |
| `dN` | LED breath depth, 0..64 (default 16 ≈ ±6 % subtle) |
| `pN` | LED breath period in ms, 1000..20000 (default 4000) |
| `w` | Run `ledWalk` (lights LEDs 0..13 in sequence — blocks ~14 s) |
| `k` | (Phase B placeholder) recalibrate current-sense baseline |
| `s` | Toggle current-sense scope mode |
| `r` | Dump reed state — raw (LOW=magnet present, HIGH=open) and debounced |
| `m` | Mute / unmute the periodic `[PLOT]` stream |

The LED driver addresses Matrix B of the IS31FL3731 directly via `setLEDPWM(lednum, pwm, 0)` — `lednum` 8..15 are CB1 (top row, LEDs 1..8) and 24..29 are CB2 (bottom row, LEDs 9..14), per IS31FL3731 datasheet Rev F Table 7. Breath modulation uses a 64-entry sine LUT with linear interpolation, then a 256-entry 2.2 gamma table — smooth and FPU-free.

## Bring-up checklist

1. **Boot output:** confirm `[APP] PIN_*=… (GPIO N)` lines match the XIAO ESP32-C6 expected values (D0=0, D10=18, D2=2, D3=21, D6=16, D7=17). If they don't, the board selected in Tools→Board is wrong.
2. **Walk:** send `w` → 14 LEDs light in sequence with `[LED] walk i=N lednum=M`. If the physical order differs from top→bottom, reorder `LED_MAP[]` in `pins.h`.
3. **D7 indicator:** at boot with no container, D7 should be dim and steady.
4. **LEDs ambient toggle:** press the button briefly with no container → ring fades in to user level. Press again → fades out.
5. **Mist trigger:** dock a container with a magnet → after the 500 ms dwell, `[APP] -> RUNNING`, D3 goes HIGH (red LED2 lights), mist + ring ramp up together over ~800 ms.
6. **Mist shut-off:** lift the container → `[REED] removed` after 100 ms, **mist hard-stops** (no fade), ring continues fading down, D7 returns to dim.
7. **Long-press ramp:** while RUNNING (or `IDLE_LEDS_ON`), hold the button → ring + mist dim together. Release. Hold again → ramp the other direction.
8. **Current data (Phase B prep):** with a docked container at full level, send `s` and open Serial Plotter. Capture `mean_mA, var_mA2` traces for full water, low water, dry disc, no disc.

## Files

| File | Purpose |
|---|---|
| `BlockKit_Test.ino` | `setup()`, `loop()`, 3-state machine, level smoother, serial parser |
| `pins.h` | Pin numbers, tunables, enums — single source of truth |
| `mist.ino` | `mistApply(level)` / `mistHardStop()` — boost gating + PWM scaling |
| `container.ino` | Reed switch debounce + insert/remove edges |
| `button.ino` | Button debounce + short / long-press events |
| `led_driver.ino` | IS31FL3731 (Matrix B) — uniform breathing + LUT interpolation + `ledWalk` |
| `status_led.ino` | D7 white LED — dim-on or off |
| `current_sense.ino` | INA180 ADC at 1 kHz, rolling mean+variance, scope mode |

All `.ino` files are auto-concatenated by the Arduino IDE — there is no build system to configure.

## Phase B follow-ups (not in this build)

- Add `LOW_WATER` state to the machine: warn user (red LED + serial), start a 10-minute shutoff timer, refill cancels.
- Adaptive baseline capture (5 s slosh skip + 25 s average) into RAM.
- Variance threshold tuned from Phase A scope data.
- Battery-low detection requires a V0.2 hardware change (VBAT voltage divider).
