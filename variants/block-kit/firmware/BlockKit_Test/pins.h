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

// ---------- IS31FL3731 LED ring ----------
//
// 14 populated LEDs on Matrix B (CB1/CB2). We address them directly via
// setLEDPWM(lednum, pwm, 0) — drawPixel goes to Matrix A which isn't populated.
//
// LED numbering reference (IS31FL3731 datasheet Rev F, Table 7):
//   CB1.C1-9..C1-16  -> lednum 8..15  (LEDs 1..8, top)
//   CB2.C2-9..C2-14  -> lednum 24..29 (LEDs 9..14, bottom)
constexpr uint8_t  LED_COUNT             = 14;
constexpr uint8_t  LED_IS31_ADDR         = 0x74;
constexpr uint8_t  LED_IS31_FRAME        = 0;       // begin() activates frame 0
constexpr uint16_t LED_TICK_MS           = 20;      // 50 fps render — smooth motion

// Uniform breathing — all 14 LEDs same brightness, modulated around the
// current base level via a 64-entry sine LUT with linear interpolation between
// entries (so the curve is continuous to the eye, not stair-stepped).
// Depth is the half-amplitude of the modulation; the user asked for subtle.
constexpr uint8_t  LED_BREATH_DEPTH      = 16;      // 16/255 ≈ ±6% on each side
constexpr uint16_t LED_BREATH_PERIOD_MS  = 4000;    // one inhale/exhale cycle

// 14 LED numbers on Matrix B, ordered top (index 0) to bottom (index 13).
// With uniform breathing the order only matters for ledWalk() verification.
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
//   ┌───────────────────┐   short-press   ┌───────────────────┐
//   │  IDLE_LEDS_OFF    │ ──────────────► │  IDLE_LEDS_ON     │
//   │  no container     │ ◄────────────── │  no container     │
//   │  target = 0       │   short-press   │  target = user    │
//   │  D7 dim, ring off │                 │  D7 dim, ring on  │
//   └─────────┬─────────┘                 └─────────┬─────────┘
//             │                                     │
//        container ↓                            container ↓
//             │                                     │
//             ▼                                     ▼
//                  ┌──────────────────────────┐
//                  │       RUNNING            │
//                  │  container docked        │
//                  │  mist PWM @ scaled level │
//                  │  target = user           │
//                  │  D7 off, ring breathing  │
//                  └──────────┬───────────────┘
//                             │ short-press OR container removed
//                             ▼
//                       IDLE_LEDS_OFF
enum class AppState : uint8_t {
  IDLE_LEDS_OFF,
  IDLE_LEDS_ON,
  RUNNING,
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
