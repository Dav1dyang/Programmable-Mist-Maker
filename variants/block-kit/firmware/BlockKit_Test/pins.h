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

// Uniform breathing (LedMode::BREATH) — all 14 LEDs share brightness modulated
// around the current base level via a 64-entry sine LUT with linear inter-
// polation between entries (so the curve is continuous to the eye, not stair-
// stepped). Used while idle (no container) and during ramp-up before swirl
// kicks in. Depth is the half-amplitude; user asked for subtle high dim.
constexpr uint8_t  LED_BREATH_DEPTH      = 16;      // 16/255 ≈ ±6% on each side
constexpr uint16_t LED_BREATH_PERIOD_MS  = 4000;    // one inhale/exhale cycle

// Vertical chase (LedMode::SWIRL) — used while a container is docked and the
// ring is at full brightness. A bright "head" rises continuously from bottom
// (index 13) toward top (index 0), with a SWIRL_TAIL_LEDS-long fading tail
// trailing behind it. Wraps seamlessly: as one head leaves the top, the
// trailing tail re-emerges from the bottom on the next cycle.
//
// In TRANSITION_FROM_RUNNING (container just removed), the swirl decelerates
// proportionally to the current level so motion slows as it dims, producing
// a soft "wind down" feel.
constexpr uint16_t SWIRL_PERIOD_MS       = 1500;    // one head traverses 14 LEDs
constexpr uint8_t  SWIRL_TAIL_LEDS       = 6;       // fading tail length, in LED units

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
// here. Step sizes are tuned for ~800 ms 0→255 ramp (luxurious).
constexpr uint16_t LEVEL_SMOOTH_TICK_MS  = 10;      // smoother tick
constexpr uint8_t  LEVEL_SMOOTH_STEP_UP  = 3;       // ~850 ms 0→255 (luxurious)
constexpr uint8_t  LEVEL_SMOOTH_STEP_DN  = 4;       // slightly faster fade-out
// Fast step-up used ONLY for the post-removal breath restore so the entire
// container-lift cinematic (swirl-fade ~640 ms + breath restore) lands near 1 s.
constexpr uint8_t  LEVEL_SMOOTH_STEP_UP_FAST = 6;   // ~425 ms 0→255

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
//   TRANSITION_FROM_RUNNING (swirl-fade + breath restore) before landing
//   back in IDLE_LEDS_ON, giving a cinematic ~1 s wind-down.
//
//   ┌─────────────────┐   short-press   ┌─────────────────────────────┐
//   │ IDLE_LEDS_OFF   │ ──────────────► │ IDLE_LEDS_ON  (default boot) │
//   │ ring dark, D7 ▒│ ◄────────────── │ ring breath ✻, D7 ▒          │
//   └────────┬────────┘   short-press   └─────────────┬───────────────┘
//            │                                        │
//       container ↓                              container ↓
//            ▼                                        ▼
//                ┌────────────────────────────────────────────┐
//                │  RUNNING                                   │
//                │  fade up (breath) ➜ swirl at max          │
//                │  mist on, D7 off, ring chase ↑             │
//                └─────────────────┬──────────────────────────┘
//                                  │ container removed
//                                  ▼
//                ┌────────────────────────────────────────────┐
//                │  TRANSITION_FROM_RUNNING                   │
//                │  mist hard-stopped, swirl decelerates &   │
//                │  dims to 0, then auto-enters IDLE_LEDS_ON  │
//                │  with a fast breath fade-up                │
//                └────────────────────────────────────────────┘
//   Short-press from RUNNING / TRANSITION → IDLE_LEDS_OFF (skips cinematic).
enum class AppState : uint8_t {
  IDLE_LEDS_OFF,
  IDLE_LEDS_ON,
  RUNNING,
  TRANSITION_FROM_RUNNING,
};

// LED render mode — owned by the state machine, consumed by led_driver.ino.
// State transitions call ledSetMode(); led_driver dispatches per-tick.
enum class LedMode : uint8_t {
  BREATH,   // uniform sine-modulated breath, all 14 LEDs same brightness
  SWIRL,    // rising-chase head + fading tail, bottom→top
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
