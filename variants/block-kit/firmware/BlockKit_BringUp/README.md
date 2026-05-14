# BlockKit_BringUp — bare-metal per-feature test sketch

The tiny sketch you flash when something on the bench feels off and you want to
isolate the hardware before re-engaging the full production UX. **No state
machine, no animations, no smoother** — press the button and verify each
peripheral works in isolation.

For normal daily use flash [`../BlockKit_Production/`](../BlockKit_Production/)
instead.

## What it does

| Trigger | Effect |
|---|---|
| Press button (D6) | Toggle mist (D0 @ 50 % PWM) + boost rail (D3) + all 14 LEDs at PWM 80 |
| Send `t` | Same as a button press |
| Send `w` | LED chase: each of the 14 LEDs lights for 1 s in top→bottom order |
| Send `c` | Current-sense dump: mean / min / max ADC count and mA of 100 reads |
| Send `h` | Help |

Every 250 ms a `[STAT]` line prints with raw button, raw reed, mist on/off,
raw ADC and computed mA — so you can leave the Serial Monitor open and
visually confirm the hardware is responding without waiting for an event.

## Bring-up checklist

1. **Boot output** — confirm the `[APP] PIN_*=… (GPIO N)` lines match the
   XIAO ESP32-C6 mapping (D0=0, D2=2, D3=21, D6=16, D7=17, D10=18). If not,
   the wrong board is selected in Tools→Board.
2. **D7 status LED** — boots dim (~10 %). Goes off when the toggle is ON.
3. **Button** — pressing should print `[BTN] press` followed by an `[OUT]`
   line. If `[BTN]` never appears, check the 10 k pull-down on D6 and the
   button PCB harness.
4. **Boost rail** — when the toggle goes ON, the red LED2 on the Driver
   PCB should light (D3 high = TPS61023 enable).
5. **Mist** — once boost is up the piezo should mist. Hold the container
   above it to verify.
6. **LED ring** — all 14 LEDs come up at PWM 80 (uniform bright). If
   IS31FL3731 fails to init you'll see `[LED] IS31FL3731 not found at 0x74`
   on the Serial banner; check I2C wiring (D4/D5) and that the LED PCB has
   3 V3 power.
7. **LED order** — send `w`. The chase walks LEDs in the order defined by
   `LED_MAP[]` in `pins.h`; physical order should be top→bottom. If
   reversed, swap the array.
8. **Reed switch** — drop a magnet on the reed; `[STAT]` row shows
   `reed=1`. Lift; back to `reed=0`. (Note: the BringUp sketch does NOT
   gate mist on the reed — that's a production-firmware feature.)
9. **Current sense** — with the toggle OFF send `c`; mA should be near 0.
   Toggle ON, send `c` again; mA should jump to ~100–200 mA (depending on
   water load).

## Install

Same as production:

1. Arduino IDE 2.x with **esp32 by Espressif Systems** v3.x.
2. Install [David's IS31FL3731 fork](https://github.com/Dav1dyang/Adafruit_IS31FL3731)
   into the Arduino libraries folder.
3. Open `BlockKit_BringUp.ino`, select board **XIAO_ESP32C6**,
   USB CDC On Boot **Enabled**, Upload.

## Files

| File | Purpose |
|---|---|
| `BlockKit_BringUp.ino` | Everything — single-file sketch |
| `pins.h` | GPIO assignments + frequency / LED-map constants (no UX tunables) |

Only one library dependency: `Adafruit_IS31FL3731` (David's fork). No
`Adafruit_BusIO` / `Adafruit_GFX` direct calls beyond what the fork pulls in.
