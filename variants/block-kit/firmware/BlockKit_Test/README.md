# BlockKit_Test — Phase A bring-up firmware

Block Kit V0.1 firmware that exercises every subsystem (mist PWM, reed switch, button, IS31FL3731 LED ring, D7 breathing LED) **plus** a "scope mode" that streams the INA180 current data so the water-detection algorithm can be designed from real bench data later (Phase B).

Phase A intentionally has **no water-level classifier**. The two states are `IDLE` and `RUNNING`; the reed switch is the primary on-gesture (magnetic-on), the button is a manual override.

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

   The stock Adafruit version drives Matrix A only and won't light the populated side of the Block Kit's LED matrix. Adafruit_BusIO and Adafruit_GFX (its transitive dependencies) install fine from Library Manager.
3. Open `BlockKit_Test.ino`. Select board **XIAO_ESP32C6**, USB CDC On Boot **Enabled**.
4. **Verify** to compile, then **Upload**.

## Serial console (115200 baud)

On boot you'll see `[APP] Block Kit V0.1 bring-up (Phase A)` followed by the command list. Every log line is tagged so you can grep one subsystem:

```
[APP] -> RUNNING
[MIST] on
[REED] inserted
[BTN] long-press start, dir=-1
[LED] overall=80
[CUR] scope mode ON
[PLOT] 178.4,12.3,1
```

The single `[PLOT] mean_mA,var_mA2,state` CSV line at 20 Hz is what the Arduino Serial Plotter ingests for live graphing. When scope mode is on, `[PLOT]` switches to `raw_mA,mean_mA,var_mA2` at 100 Hz instead.

### Commands

| Cmd | Effect |
|---|---|
| `help` | Print this list |
| `1` | Mist on (requires container docked) |
| `0` | Mist off |
| `t` | Toggle mist |
| `a0` / `a1` | LED swirl animation off / on |
| `bN` | LED **max** brightness — peak of the wave, `0..255` (e.g. `b200`) |
| `cN` | LED **min** brightness — trough of the wave, `0..255` (e.g. `c12`) |
| `pN` | LED swirl period in ms, `1000..20000` |
| `lN` | LED wavelength in LEDs per cycle, `2..64` (default 18 = gentle breath) |
| `w` | Run `ledWalk` (lights LEDs 0..13 in sequence — blocks ~14 s) |
| `k` | (Phase B placeholder) recalibrate current-sense baseline |
| `s` | Toggle current-sense scope mode |
| `r` | Dump reed state — raw (LOW=magnet present, HIGH=open) and debounced |

The LED driver addresses Matrix B of the IS31FL3731 directly via `setLEDPWM(lednum, …)` — `lednum` 8..15 are CB1 (top row, LEDs 1..8) and 24..29 are CB2 (bottom row, LEDs 9..14), per IS31FL3731 datasheet Rev F Table 7. The wave shape comes from a 64-entry integer sine LUT followed by a 256-entry gamma 2.2 LUT for perceptual smoothness.

## Bring-up checklist

1. **LEDs:** run `w` → confirm 14 LEDs light in the order you expect (top → bottom). If physical order differs, reorder `LED_POSITIONS[]` in `pins.h`.
2. **D7 breathing:** with no container docked, the white LED should pulse with a ~3 s breath period.
3. **Mist trigger:** dock a container with a magnet → after the 500 ms dwell, mist starts and the swirl animation begins. Red LED2 on the Driver PCB lights because D3 is now HIGH (boost enabled).
4. **Mist shut-off:** lift the container → mist stops within ~100 ms, D3 returns LOW, breathing resumes.
5. **Manual override:** with the container docked, press the button briefly → mist toggles.
6. **LED brightness ramp:** while running, hold the button → swirl dims; release; hold again → brightens (direction auto-reverses).
7. **Current data:** run `s` and open Serial Plotter. With the container docked and water full, observe the `mean_mA, var_mA2` traces. Repeat with low water and with the disc dry. **This is the bench data Phase B needs.**

## Files

| File | Purpose |
|---|---|
| `BlockKit_Test.ino` | `setup()`, `loop()`, state machine, serial command parser |
| `pins.h` | Pin numbers, tunables, enums — single source of truth |
| `mist.ino` | 108.7 kHz PWM, D3 boost enable, power-gating |
| `container.ino` | Reed switch debounce + insert/remove edges |
| `button.ino` | Button debounce + short / long-press events |
| `led_driver.ino` | IS31FL3731 (Matrix B) + LUT-based swirl + `ledWalk` |
| `status_led.ino` | D7 white LED breathing pulse |
| `current_sense.ino` | INA180 ADC at 1 kHz, rolling mean+variance, scope mode |

All `.ino` files are auto-concatenated by the Arduino IDE — there is no build system to configure.

## Phase B follow-ups (not in this build)

- Add `LOW_WATER` state to the machine: warn user (red LED + serial), start a 10-minute shutoff timer, refill cancels.
- Adaptive baseline capture (5 s slosh skip + 25 s average) into RAM.
- Variance threshold tuned from Phase A scope data.
- Battery-low detection requires a V0.2 hardware change (VBAT voltage divider).
