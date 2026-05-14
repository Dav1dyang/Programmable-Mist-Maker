# BlockKit_Production — Block Kit V0.1 daily-use firmware

Production firmware for the Block Kit. Combines the full Phase A UX
(mist PWM, reed switch, button override with hide/show, dual-mode LED
ring with mist-synced wave drive, INA180 current sense + scope mode)
with **WiFi onboarding** (WiFiManager captive portal), **OTA via the
Arduino IDE** (ArduinoOTA / network port), and a **single-page web
UI** for live status, runtime config of every tunable, and debug
commands.

For raw bench-level per-feature verification (no animations, no state
machine, no WiFi) flash the sibling sketch at
[`../BlockKit_BringUp/`](../BlockKit_BringUp/) instead. Use BringUp
when something on the hardware feels off; use this folder for daily
use.

Phase A intentionally has **no water-level classifier**. The reed switch is the primary on-gesture (magnetic-on); the button short-press hides the LED strip without touching the mist; long-press ramps the level. One unified `g_userLevel` variable drives both mist PWM duty and LED ring brightness so they move together; while `RUNNING`, the mist drive is additionally modulated by the wave swell at the piezo position so the mist visibly pulses with the LED animation.

## States

Three container-driven states, all transitions handled in `BlockKit_Production.ino::loop()`. A level smoother fades `g_currentLevel` toward whatever the new state's target is in tiny per-tick steps (+2 / 10 ms ≈ 1.3 s 0→255), so every brightness change reads as a continuous slide. The LED driver runs an automatic 1.1 s **crossfade** whenever the mode swaps between BREATH and WAVE — there is never a snap.

When the container is removed (RUNNING → TRANSITION_FROM_RUNNING → IDLE), the level fades smoothly to 0 with the wave still rendering (so the dim reads as continuous), then on entering IDLE the breath swells back in over ~1.3 s using the *slow* smoother step — no quick flash; the strip just eases back into the deep-dim idle.

LED visibility is **orthogonal** to state: a separate boolean (`g_ledsHidden`, toggled by the short-press button) hides or shows the strip with its own ~640 ms fade. Mist is unaffected by this flag — short-pressing only mutes the visuals; it never stops the diffuser.

| State                     | When                              | Mist                                              | LED ring                                          | D7 white    |
|---------------------------|-----------------------------------|---------------------------------------------------|---------------------------------------------------|-------------|
| `IDLE` *(default boot)*   | No container                      | off                                               | deep-dim exp(sin) BREATH; exhale dwells at black  | dim (~10 %) |
| `RUNNING`                 | Container docked                  | **Wave-modulated** at piezo position — mist pulses with the LED swell; trough ≈ 36 % of set level | WAVE — every LED always lit, single slow gaussian swell rising bottom→top over 7.5 s | off |
| `TRANSITION_FROM_RUNNING` | Container just lifted             | hard-stopped                                      | Wave dims to 0 (~0.85 s), then auto-enters `IDLE` for the BREATH crossfade | dim (~10 %) |

### Transition map

| From state                  | Event                              | To state                  | Notes                                                                                  |
|-----------------------------|------------------------------------|---------------------------|----------------------------------------------------------------------------------------|
| `IDLE`                      | Container inserted (reed 500 ms)   | `RUNNING`                 | BREATH→WAVE crossfade — dim breath dissolves into slow swell                           |
| `RUNNING`                   | Container removed (reed 100 ms)    | `TRANSITION_FROM_RUNNING` | **Mist hard-stops** (safety); wave dims naturally as baseLevel ramps 255→0             |
| `TRANSITION_FROM_RUNNING`   | Smoother lands at 0                | `IDLE`                    | Mode→BREATH (WAVE→BREATH crossfade); fast restore (~0.64 s)                            |
| `TRANSITION_FROM_RUNNING`   | Container re-inserted              | `RUNNING`                 | Crossfade engine takes over mid-fade — clean recovery, no special case                 |
| any state                   | Button **short-press**             | (no state change)         | Toggles `g_ledsHidden`. LED render fades to/from 0 over ~640 ms. Mist unchanged.       |
| `IDLE` or `RUNNING`         | Button **long-press**              | (no state change)         | Ramps `g_userLevel`; mist + LED brightness follow live (wave traverse rate unchanged)  |

### One level + an orthogonal hide flag

`g_userLevel` (0..255) is the user-set "intensity". It controls:
- **Mist PWM duty** = `(level × MIST_DUTY_MAX) / 255` — level 255 ⇒ 50 % duty (full mist). In `RUNNING` the duty is *further* modulated by the wave swell at the piezo position — see "Wave-mist sync" below.
- **LED ring brightness** — scales the entire render (BREATH cap and WAVE base+swell) uniformly.

`g_targetLevel` is what the current state wants the level to be (0 in `TRANSITION_FROM_RUNNING`, `g_userLevel` otherwise). `g_currentLevel` is smoothed toward target each tick in deliberately tiny steps so the fade reads as continuous, not stepped.

`g_ledsHidden` is the short-press toggle. When set, a per-LED scaler (`g_ledScale`, 0..255 smoothed over ~640 ms) multiplies the `g_currentLevel` handed to `ledRender()`. Mist does NOT see this scaler — so blanking the visuals never stops the diffuser. Long-press to adjust the level still works whether the LEDs are hidden or not (you'll feel the change through the mist).

### Wave-mist sync (RUNNING only)

While `RUNNING`, mist drive is no longer a flat `g_currentLevel`. Each tick the firmware samples the wave's gaussian swell at the **piezo position** (1 LED above index 0, where the piezo disc physically sits) and uses it to modulate the mist:

```
factor (Q8) = MIST_WAVE_TROUGH_Q8 + ((256 - MIST_WAVE_TROUGH_Q8) × gauss_at_piezo) / 256
mist_level  = (g_currentLevel × factor) / 256
```

Because the piezo sits *above* the top LED, the gaussian peaks at the piezo **after** the wave has visibly crossed the top of the strip — so the mist "continues to grow" as the swell passes the top, then dims back down together with the top LED as the wave exits off-screen above. At max user level (255) the mist swings between ~36 % and 100 % of full duty — visible pulse, but proportional to the wave's own 92/255 trough-to-peak ratio so the LEDs you see and the mist you feel feel like one motion.

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
3. Install the **WiFiManager** library (by tzapu) via Arduino IDE → Sketch → Include Library → Manage Libraries → search "WiFiManager tzapu".
4. Open `BlockKit_Production.ino`. Select board **XIAO_ESP32C6**, USB CDC On Boot **Enabled**.
5. **Verify** to compile, then **Upload**.

## WiFi setup, OTA, and the web UI

On first boot the device has no saved credentials. It spins up a WPA2
AP named `BlockKit-Setup-XXXX` (password `blockkit-setup`). Join it
from a phone or laptop — your OS should auto-launch the captive
portal; otherwise visit `http://192.168.4.1/`. Pick your home WiFi,
enter the WiFi password, **and set an admin password** (used for
config writes + OTA). Save. The device reboots and joins your home
WiFi.

Once online, browse to **`http://blockkit.local/`** (or the IP shown
on the Serial banner). The web UI has four tabs:

- **Status** — live cards (state, mist, current mA, uptime) updating
  every 250 ms via Server-Sent Events; raw button/reed + sparkline.
  Open to anyone on the LAN.
- **Config** — sliders/inputs for every tunable in `config.h` (LED
  timing + brightness peak/low, mist max/low, wave-sync trough,
  button/reed timings, smoother steps, …). Save prompts for the
  admin password and persists to NVS.
- **Debug** — raw IO live, plus buttons for LED walk, hide/show
  LEDs, scope toggle, reboot, forget WiFi. Commands require the
  admin password.
- **About** — device info (mDNS hostname, IP, MAC, RSSI), firmware
  version + build date, OTA hint.

**OTA via Arduino IDE:** once the device is on the same WiFi as your
laptop, it shows up under Arduino IDE → Tools → Port → `blockkit at
<ip> (esp32)`. Pick it, hit Upload, enter the admin password when
prompted. The firmware hard-stops the mist (boost rail LOW, MOSFET
gate pulled down) before flash erase begins — no surprise misting
mid-upload.

To re-enter setup (e.g. moving the device to a new WiFi), click
**Forget WiFi** in the Debug tab.

## Serial console (115200 baud)

On boot you'll see the banner, the resolved pin map, and the command list. Every log line is tagged so you can grep one subsystem:

```
[APP] Block Kit V0.1 bring-up (Phase A)
[APP] PIN_MIST_PWM=D0 (GPIO 0)
[APP] PIN_REED=D10 (GPIO 18)
[APP] state=IDLE (boot, LEDs visible)
[REED] inserted
[APP] -> RUNNING
[MIST] on (boost up)
[STAT] state=RUNNING leds=visible reed=1 btn=0 user=255 cur=140 mist=1 mean_mA=178.2
[PLOT] 178.4,12.3,1
[BTN] long-press start, dir=-1
```

- `[STAT]` prints once a second — a human-readable subsystem snapshot.
- `[PLOT]` is the Arduino Serial Plotter–friendly CSV: `mean_mA, var_mA2, state` at 20 Hz when scope mode is off, or `raw_mA, mean_mA, var_mA2` at 100 Hz when scope mode is on.
- Send `m` to mute `[PLOT]` while you read other lines.

### Commands

| Cmd | Effect |
|---|---|
| `help` | Print this list |
| `l` / `L` / `t` | Hide LEDs / show LEDs / toggle visibility (mist is unaffected) |
| `vN` | Set `g_userLevel` directly, 0..255 (e.g. `v180`) |
| `w` | Run `ledWalk` (lights LEDs 0..13 in sequence — blocks ~14 s) |
| `k` | (Phase B placeholder) recalibrate current-sense baseline |
| `s` | Toggle current-sense scope mode |
| `r` | Dump reed state — raw (LOW=magnet present, HIGH=open) and debounced |
| `m` | Mute / unmute the periodic `[PLOT]` stream |

The LED driver addresses Matrix B of the IS31FL3731 directly via `setLEDPWM(lednum, pwm, 0)` — `lednum` 8..15 are CB1 (top row, LEDs 1..8) and 24..29 are CB2 (bottom row, LEDs 9..14), per IS31FL3731 datasheet Rev F Table 7. BREATH uses a 64-entry exp(sin) curve LUT (asymmetric — lingers at black on the exhale for a dramatic dim-idle); WAVE uses a 64-entry gaussian LUT for the swell shape (σ=4 LEDs, so the bump occupies ~⅓ of the strip at any moment). Both are linear-interpolated for sub-quantum smoothness and pass through a 256-entry 2.2 gamma table — smooth and FPU-free. Mode swaps run a 1.1 s linear crossfade in pre-gamma space.

## Bring-up checklist

1. **Boot output:** confirm `[APP] PIN_*=… (GPIO N)` lines match the XIAO ESP32-C6 expected values (D0=0, D10=18, D2=2, D3=21, D6=16, D7=17). If they don't, the board selected in Tools→Board is wrong.
2. **Walk:** send `w` → 14 LEDs light in sequence with `[LED] walk i=N lednum=M`. If the physical order differs from top→bottom, reorder `LED_MAP[]` in `pins.h`.
3. **D7 indicator:** at boot with no container, D7 should be dim and steady.
4. **Short-press hides LEDs only:** press the button briefly (with or without a container) → `[APP] LEDs hidden …`, the strip fades to dark over ~640 ms. Mist (if a container is docked) keeps running at the same level. Press again → `[APP] LEDs visible`, strip fades back in.
5. **Mist trigger + wave-sync:** dock a container with a magnet → after the 500 ms dwell, `[APP] -> RUNNING`, D3 goes HIGH (red LED2 lights), mist + ring ramp up together. Once steady, the **mist visibly pulses with the swell** — the mist peaks just after the LED swell crosses the top of the strip, then dims back down together with it.
6. **Mist shut-off:** lift the container → `[REED] removed` after 100 ms, **mist hard-stops** (no fade), ring continues fading down, D7 returns to dim.
7. **Long-press ramp:** while RUNNING (or `IDLE`), hold the button → ring + mist dim together (mist still pulses with the wave on top of the dim). Release. Hold again → ramp the other direction. The ramp clamps at 0/255; long-pressing to 0 leaves you at level 0, not in a special "off" state — the LEDs stay visible (just dark), and the next long-press in the other direction brightens back up.
8. **Current data (Phase B prep):** with a docked container at full level, send `s` and open Serial Plotter. Capture `mean_mA, var_mA2` traces for full water, low water, dry disc, no disc.

## Files

| File | Purpose |
|---|---|
| `BlockKit_Production.ino` | `setup()`, `loop()`, 3-state machine, level smoother, serial parser, web/OTA hook-up |
| `config.h` / `config.ino` | Runtime config struct + NVS load/save + SHA-256 password hashing |
| `wifi_setup.ino` | WiFiManager captive portal + STA reconnect watcher |
| `ota.ino` | ArduinoOTA bring-up with mist-safety `onStart` hook |
| `web_server.ino` | Synchronous WebServer + SSE stream + JSON API |
| `web_ui.h` | PROGMEM single-page HTML/CSS/JS app |
| `log_buffer.ino` | RAM ring buffer mirror of Serial for the Debug tab |
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
