// Block Kit V0.1 — single source of truth for pins, constants, and tunables.
// Every `.ino` reads from here; never duplicate a magic number elsewhere.

#pragma once
#include <Arduino.h>

// ---------- GPIO assignments ----------
//
// The Block Kit V0.1 PCB physically only mates with a XIAO socket. The XIAO
// platform headers already expose D0..D10 as C++ constants that match the
// silkscreened pin labels, so on XIAO boards we just use those directly —
// no preprocessor remapping, nothing for the C preprocessor to get wrong.
//
// For non-XIAO ESP32 dev boards (Adafruit QT Py family) we keep the sketch
// buildable as a porting / "does it compile" target, by picking GPIOs from
// labels (A0..A3, TX, RX) that those board variants do expose. The Block
// Kit PCB itself does not mate with these boards — the firmware will run,
// but won't drive any real hardware.
//
// Any other board → #error so silent miswiring (the original V0.1 bug) is
// impossible.

#if defined(ARDUINO_XIAO_ESP32C6) || \
    defined(ARDUINO_XIAO_ESP32S3) || \
    defined(ARDUINO_XIAO_ESP32S3_PLUS)

  // ---- XIAO socket form factor — Block Kit V0.1 production target ----
  // Pin assignments match the Driver PCB schematic exactly. See
  // variants/block-kit/hardware/README.md for the inter-board wiring map.
  constexpr uint8_t PIN_MIST_PWM     = D0;   // 108.7 kHz PWM to MOSFET gate
  constexpr uint8_t PIN_REED         = D10;  // Reed switch (INPUT_PULLUP), LOW=docked
  constexpr uint8_t PIN_CURRENT_ADC  = D2;   // INA180A3 via 1k+1uF filter
  constexpr uint8_t PIN_BOOST_EN     = D3;   // TPS61023 EN (HIGH = 5V rail on)
  constexpr uint8_t PIN_BUTTON       = D6;   // Active-HIGH, PCB 10k pull-down
  constexpr uint8_t PIN_STATUS_LED   = D7;   // White LED, breathing in idle

#elif defined(ARDUINO_ADAFRUIT_QTPY_ESP32S3_NOPSRAM) || \
      defined(ARDUINO_ADAFRUIT_QTPY_ESP32S3_N4R2)    || \
      defined(ARDUINO_ADAFRUIT_QTPY_ESP32S2)         || \
      defined(ARDUINO_ADAFRUIT_QTPY_ESP32C3)         || \
      defined(ARDUINO_ADAFRUIT_QTPY_ESP32_PICO)

  // ---- Adafruit QT Py family — compile-only target, not real hardware ----
  // QT Py boards expose A0..A3 + TX/RX/SDA/SCL labels on the silkscreen, but
  // no XIAO-style D0..D10. The Block Kit PCB does not physically mate with
  // any QT Py — this block exists so the sketch still builds for porting.
  // I2C (SDA/SCL) comes from Wire.begin() defaults and isn't pinned here.
  constexpr uint8_t PIN_MIST_PWM     = TX;   // generic output header pin
  constexpr uint8_t PIN_REED         = RX;   // generic input header pin
  constexpr uint8_t PIN_CURRENT_ADC  = A0;   // ADC-capable header pin
  constexpr uint8_t PIN_BOOST_EN     = A3;
  constexpr uint8_t PIN_BUTTON       = A2;
  constexpr uint8_t PIN_STATUS_LED   = A1;

#else
  #error "Block Kit V0.1 firmware: please select a supported board in Arduino IDE \
> Tools > Board. Supported: XIAO_ESP32C6 (production), XIAO_ESP32S3, \
XIAO_ESP32S3_PLUS, or any Adafruit QT Py ESP32 variant (compile-only)."
#endif

// ---------- Mist PWM ----------
constexpr uint32_t MIST_FREQ_HZ    = 108700;  // ceramic disc resonance
constexpr uint8_t  MIST_PWM_RES    = 8;       // 8-bit -> 0..255 duty
constexpr uint8_t  MIST_DUTY_MAX   = 127;     // 50% duty = full mist (peak level)

// ---------- Status LED (D7) ----------
// Simple on-or-off "waiting" indicator. Dim PWM when no container is docked,
// fully off when the container is on the dock.
constexpr uint32_t STATUS_LED_FREQ_HZ = 5000;
constexpr uint8_t  STATUS_LED_RES     = 8;
constexpr uint8_t  STATUS_LED_DIM_DUTY = 24;  // ~10% — visible in a dark room, not bright

// ---------- INA180 current sense ----------
// INA180A3 gain = 100 V/V, R_sense = 30 mOhm  =>  V_out = I * 3.0 V/A
// => mA = adc_volts * 1000 / 3.0
constexpr float    CURRENT_SENSE_FACTOR = 3.0f;     // V per A
constexpr uint16_t CURRENT_SAMPLE_HZ    = 1000;     // ADC sample rate target
constexpr uint16_t CURRENT_WINDOW_N     = 256;      // sliding window length
constexpr uint32_t SCOPE_PRINT_HZ       = 100;      // scope-mode print rate

// ---------- Button (D6) timing ----------
constexpr uint16_t BUTTON_DEBOUNCE_MS   = 50;
constexpr uint16_t BUTTON_LONGPRESS_MS  = 500;
constexpr uint16_t BUTTON_LONGTICK_MS   = 13;       // ~80 steps/sec while held

// ---------- Reed switch (D10) timing ----------
constexpr uint16_t REED_INSERT_DWELL_MS = 500;      // require stable LOW for 500 ms before auto-start
constexpr uint16_t REED_REMOVE_DWELL_MS = 100;      // faster removal so shut-off isn't sluggish

// ---------- IS31FL3731 LED strip (14 LEDs, vertical) ----------
//
// 14 populated LEDs on Matrix B (CB1/CB2), arranged as a vertical strip
// (top → bottom). LED 1 (top) corresponds to index 0; LED 14 (bottom) to
// index 13. We address each LED directly via setLEDPWM(lednum, pwm, 0) —
// drawPixel(x,y,…) goes to Matrix A which isn't populated.
//
// LED numbering reference (IS31FL3731 datasheet Rev F, Table 7):
//   CB1.C1-9..C1-16  -> lednum 8..15  (LEDs 1..8, top half)
//   CB2.C2-9..C2-14  -> lednum 24..29 (LEDs 9..14, bottom half)
constexpr uint8_t  LED_COUNT             = 14;
constexpr uint8_t  LED_IS31_ADDR         = 0x74;
constexpr uint8_t  LED_IS31_FRAME        = 0;       // begin() activates frame 0
constexpr uint16_t LED_TICK_MS           = 20;      // 50 fps render — smooth motion

// ---- BREATH (idle, no container) — "deep dim, dramatic" --------------------
//
// All 14 LEDs share the same brightness, driven by an exp(sin) curve LUT
// (ThingPulse / FastLED-style). exp(sin) lingers near zero on each exhale
// and pulses to a soft peak on each inhale — much more natural-looking than
// plain (1-cos)/2, which dwells equally at both extremes. The visible peak
// is capped to LED_BREATH_PEAK so the ring stays *very dim* in idle (the
// previous design used baseLevel±depth, which left the LEDs near full
// brightness during idle — the explicit complaint we're fixing).
//
// Period is long (5.5 s) so the breath reads as slow ambient calm, not a
// status blink.
//
// Why 80 and not lower: the IS31FL3731 is 8-bit PWM and our 2.2 gamma LUT
// maps raw 0..38 onto PWM values 0 and 1 only — so the sweep at the old
// peak (38) was effectively "off → 1 PWM → off", which the eye reads as
// blink rather than breathe. Raw peak 80 maps to PWM 10, and the LUT
// transitions through 11 distinct PWM levels (0..10) over the sweep —
// enough for a smooth perceptual gradient. PWM 10/255 ≈ 4 %, still firmly
// in dim-ambient territory.
constexpr uint8_t  LED_BREATH_PEAK       = 80;      // raw peak; ≈ PWM 10/255 = 4% post-gamma
constexpr uint16_t LED_BREATH_PERIOD_MS  = 5500;    // slow inhale → exhale → dwell at dark

// ---- WAVE (docked) — "soft broad swell, all LEDs always lit" ---------------
//
// Every LED holds at WAVE_BASE_LEVEL (always-on glow). On top of that, a
// single gaussian-shaped brightness *swell* travels slowly from below the
// strip (off-screen) up through it and off the top, then re-enters from
// below — no wrapping seam. Sigma is wide (4 LEDs) so the bump is a broad
// gradient, not a discrete blob — that's what makes it "wave" rather than
// "chase". At the swell peak the LED reaches WAVE_BASE_LEVEL + WAVE_SWELL_PEAK
// (≈255 = full).
//
// Period is 7.5 s for one swell traversal — slow & meditative, per the
// premium-ambient UX brief.
constexpr uint8_t  WAVE_BASE_LEVEL       = 92;      // ~12% post-gamma baseline — every LED always visible
constexpr uint8_t  WAVE_SWELL_PEAK       = 163;     // base + peak = 255 (full at crest)
constexpr uint16_t WAVE_PERIOD_MS        = 7500;    // one swell crosses the strip — slow, meditative
// Gaussian standard deviation in Q8 LED units (256 = 1 LED). Wider σ = softer,
// broader swell that affects more of the strip at once.
constexpr uint16_t WAVE_SIGMA_LEDS_Q8    = 4 * 256; // σ = 4 LEDs (very broad)
// The swell enters off-screen below and exits off-screen above, so we never
// see a "spawn" or "vanish" — the gaussian tail is already below the visible
// threshold at ±3σ from center. Travel span = LED_COUNT + 6σ (3σ pad each end).
constexpr int16_t  WAVE_TRAVEL_PAD_Q8    = 3 * 4 * 256;  // = 3σ in Q8 LED units = 3072

// ---- Mode crossfade (BREATH ↔ WAVE) ----------------------------------------
//
// When the LED mode changes (idle→docked or docked→idle), led_driver renders
// BOTH the previous and new mode for LED_CROSSFADE_MS, linearly blending in
// pre-gamma space. That means the breath fades OUT as the wave fades IN —
// no snap, no "fade up then start chasing" two-step. This replaces the old
// `g_pendingSwirl` flag from the previous design (which made the breath fade
// up THEN snap to chase — the user's specific complaint about the dock-
// transition feeling like "nothing").
constexpr uint16_t LED_CROSSFADE_MS      = 1100;    // 1.1 s — slow enough to read as a single gesture

// 14 LED numbers on Matrix B, ordered top (index 0) to bottom (index 13).
constexpr uint8_t LED_MAP[LED_COUNT] = {
   8,  9, 10, 11, 12, 13, 14, 15,   // CB1 row: top, LEDs 1..8
  24, 25, 26, 27, 28, 29,           // CB2 row: LEDs 9..14 (bottom)
};

// ---------- Level smoothing ----------
//
// One unified `level` variable (0..255) drives both mist PWM duty and LED ring
// brightness so they always move together. State transitions and the long-
// press button event set `g_targetLevel`; a smoother in the main loop ramps
// `g_currentLevel` toward target over time. All "smooth fade" UX comes from
// here.
//
// Steps are intentionally tiny (1–2 PWM per tick at 10 ms) so the ramp reads
// as one continuous slide rather than discrete jumps — the user complaint
// about "snappy" feel was largely the old +3/+4-per-tick stepping.
constexpr uint16_t LEVEL_SMOOTH_TICK_MS      = 10;  // smoother tick
constexpr uint8_t  LEVEL_SMOOTH_STEP_UP      = 2;   // ~1.28 s 0→255 (luxurious & smooth)
constexpr uint8_t  LEVEL_SMOOTH_STEP_DN      = 3;   // ~0.85 s 255→0 (slightly faster fade-out)
// Fast step-up used ONLY for the post-removal breath restore — keeps the full
// container-lift cinematic (wave dim ~0.85 s + breath restore) feeling brisk
// without breaking continuity. The mode crossfade itself runs in parallel.
constexpr uint8_t  LEVEL_SMOOTH_STEP_UP_FAST = 4;   // ~0.64 s 0→255

// Default level on first boot — bold first impression (full mist).
constexpr uint8_t  LEVEL_DEFAULT         = 255;
// Long-press ramp step + auto-off threshold.
constexpr uint16_t LEVEL_RAMP_TICK_MS    = 13;      // ~77 steps/sec while held
constexpr uint8_t  LEVEL_RAMP_STEP       = 1;
constexpr uint8_t  LEVEL_OFF_THRESHOLD   = 8;       // dimming below this snaps to OFF state

// ---------- Serial ----------
constexpr uint32_t SERIAL_BAUD          = 115200;
constexpr uint32_t PLOT_PRINT_HZ        = 20;       // [PLOT] CSV rate when scope mode off

// ---------- Top-level state machine ----------
//
//   Boot lands in IDLE_LEDS_ON (soft breath waiting). Short-press toggles to
//   IDLE_LEDS_OFF and back. Container docking always wins: any state +
//   container → RUNNING. Container removal in RUNNING goes through a brief
//   TRANSITION_FROM_RUNNING (wave decelerates + dims to 0, then breath fades
//   back in) before landing back in IDLE_LEDS_ON.
//
//   The BREATH ↔ WAVE swap on dock/undock is always a CROSSFADE (mixed in
//   pre-gamma space by led_driver), not a snap. So when you dock a container
//   in idle, the dim breath dissolves smoothly into a slow swelling wave —
//   reads as one continuous gesture, not "fade up then start chasing".
//
//   ┌─────────────────┐   short-press   ┌─────────────────────────────┐
//   │ IDLE_LEDS_OFF   │ ──────────────► │ IDLE_LEDS_ON  (default boot) │
//   │ ring dark, D7 ▒│ ◄────────────── │ deep dim breath, D7 ▒        │
//   └────────┬────────┘   short-press   └─────────────┬───────────────┘
//            │                                        │
//       container ↓                              container ↓
//            ▼                                        ▼ (BREATH→WAVE crossfade)
//                ┌────────────────────────────────────────────┐
//                │  RUNNING                                   │
//                │  soft gaussian swell traveling bottom→top │
//                │  mist on, D7 off, every LED always lit    │
//                └─────────────────┬──────────────────────────┘
//                                  │ container removed
//                                  ▼ (mist hard-stops; wave dims to 0)
//                ┌────────────────────────────────────────────┐
//                │  TRANSITION_FROM_RUNNING                   │
//                │  level fades 255→0 with wave still         │
//                │  rendering, then auto-enters IDLE_LEDS_ON  │
//                │  → WAVE→BREATH crossfade as level rises    │
//                └────────────────────────────────────────────┘
//   Short-press from RUNNING / TRANSITION → IDLE_LEDS_OFF (skips cinematic).
enum class AppState : uint8_t {
  IDLE_LEDS_OFF,
  IDLE_LEDS_ON,
  RUNNING,
  TRANSITION_FROM_RUNNING,
};

// LED render mode — owned by the state machine, consumed by led_driver.ino.
// State transitions call ledSetMode(); led_driver dispatches per-tick and
// runs an automatic crossfade between the previous and new mode.
enum class LedMode : uint8_t {
  BREATH,   // idle: deep dim exp(sin) breath, all 14 LEDs share brightness
  WAVE,     // docked: soft gaussian swell traveling bottom→top, all LEDs lit
};

// ---------- Event enums (declared here so every .ino can use them as args) ----------
enum class ContainerEvent : uint8_t {
  None,
  Inserted,
  Removed,
};

enum class ButtonEvent : uint8_t {
  None,
  ShortPress,
  LongPressStart,
  LongPressTick,
  LongPressEnd,
};
