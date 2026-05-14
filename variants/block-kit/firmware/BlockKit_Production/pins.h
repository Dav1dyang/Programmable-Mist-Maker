// Block Kit V0.1 — single source of truth for pins, hardware constants, and
// the *defaults* for every runtime-tunable.
//
// Anything a user might reasonably want to tweak from the web UI lives here
// as a CFG_DEFAULT_* constexpr. config.ino loads those into a runtime
// `Config cfg{...}` struct (overlaying NVS-saved values if present), and
// every other .ino reads `cfg.foo` instead of the constant directly.
//
// Hardware properties (GPIOs, piezo resonance, INA180 gain, LUT-binding
// constants, the piezo's physical offset above the strip) stay as plain
// constexpr — those are facts about the board, not things to change at
// runtime.

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
  constexpr uint8_t PIN_MIST_PWM     = D0;   // 108.7 kHz PWM to MOSFET gate
  constexpr uint8_t PIN_REED         = D10;  // Reed switch (INPUT_PULLUP), LOW=docked
  constexpr uint8_t PIN_CURRENT_ADC  = D2;   // INA180A3 via 1k+1uF filter
  constexpr uint8_t PIN_BOOST_EN     = D3;   // TPS61023 EN (HIGH = 5V rail on)
  constexpr uint8_t PIN_BUTTON       = D6;   // Active-HIGH, PCB 10k pull-down
  constexpr uint8_t PIN_STATUS_LED   = D7;   // White LED, dim "waiting" indicator

#elif defined(ARDUINO_ADAFRUIT_QTPY_ESP32S3_NOPSRAM) || \
      defined(ARDUINO_ADAFRUIT_QTPY_ESP32S3_N4R2)    || \
      defined(ARDUINO_ADAFRUIT_QTPY_ESP32S2)         || \
      defined(ARDUINO_ADAFRUIT_QTPY_ESP32C3)         || \
      defined(ARDUINO_ADAFRUIT_QTPY_ESP32_PICO)

  // ---- Adafruit QT Py family — compile-only target ----
  constexpr uint8_t PIN_MIST_PWM     = TX;
  constexpr uint8_t PIN_REED         = RX;
  constexpr uint8_t PIN_CURRENT_ADC  = A0;
  constexpr uint8_t PIN_BOOST_EN     = A3;
  constexpr uint8_t PIN_BUTTON       = A2;
  constexpr uint8_t PIN_STATUS_LED   = A1;

#else
  #error "Block Kit V0.1 firmware: please select a supported board in Arduino IDE \
> Tools > Board. Supported: XIAO_ESP32C6 (production), XIAO_ESP32S3, \
XIAO_ESP32S3_PLUS, or any Adafruit QT Py ESP32 variant (compile-only)."
#endif

// ---------- Hardware constants (NOT runtime-tunable) ----------

// Mist PWM
constexpr uint32_t MIST_FREQ_HZ    = 108700;  // ceramic disc resonance
constexpr uint8_t  MIST_PWM_RES    = 8;       // 8-bit -> 0..255 duty

// Wave ↔ mist sync — the piezo physically sits 1 LED *above* the top of the
// strip (index 0). Used by waveIntensityAtPiezo() in led_driver.ino to sample
// the gaussian at that offset so the mist crest follows the wave crest as
// it passes the top of the strip. Physical placement, not a tunable.
constexpr uint16_t MIST_PIEZO_OFFSET_LEDS_Q8 = 256;  // Q8: 256 = 1 LED

// Status LED PWM
constexpr uint32_t STATUS_LED_FREQ_HZ = 5000;
constexpr uint8_t  STATUS_LED_RES     = 8;

// INA180 current sense
constexpr float    CURRENT_SENSE_FACTOR = 3.0f;     // V per A (gain×R_shunt)
constexpr uint16_t CURRENT_SAMPLE_HZ    = 1000;     // ADC sample rate target
constexpr uint16_t CURRENT_WINDOW_N     = 256;      // sliding window length
constexpr uint32_t SCOPE_PRINT_HZ       = 100;      // scope-mode print rate

// IS31FL3731 LED strip topology
constexpr uint8_t  LED_COUNT             = 14;
constexpr uint8_t  LED_IS31_ADDR         = 0x74;
constexpr uint8_t  LED_IS31_FRAME        = 0;
constexpr uint16_t LED_TICK_MS           = 20;      // 50 fps render

// Gauss LUT step in led_driver.ino is hard-coded to σ=4 LEDs (Q8 = 1024).
// Changing this requires retuning the LUT, so it's not user-tunable.
constexpr uint16_t WAVE_SIGMA_LEDS_Q8    = 4 * 256;
constexpr int16_t  WAVE_TRAVEL_PAD_Q8    = 3 * 4 * 256;  // 3σ pad each end

// 14 LED numbers on Matrix B, ordered top (index 0) to bottom (index 13).
constexpr uint8_t LED_MAP[LED_COUNT] = {
   8,  9, 10, 11, 12, 13, 14, 15,   // CB1 row: top, LEDs 1..8
  24, 25, 26, 27, 28, 29,           // CB2 row: LEDs 9..14 (bottom)
};

// Serial
constexpr uint32_t SERIAL_BAUD          = 115200;
constexpr uint32_t PLOT_PRINT_HZ        = 20;       // [PLOT] CSV rate when scope mode off

// ---------- Runtime-tunable DEFAULTS ----------
//
// Every value below is the firmware-baked default; config.ino seeds a runtime
// `Config cfg` struct from these, then overlays any NVS-saved overrides. Web
// UI writes to /api/config replace fields in `cfg` and persist them.

// LED — BREATH (idle): deep-dim exp(sin) pulse, all 14 LEDs share brightness.
constexpr uint8_t  CFG_DEFAULT_LED_BREATH_PEAK       = 80;
constexpr uint16_t CFG_DEFAULT_LED_BREATH_PERIOD_MS  = 5500;
constexpr uint8_t  CFG_DEFAULT_LED_BREATH_LOW        = 0;     // floor (0 = full black on exhale)

// LED — WAVE (docked): every LED at base level + traveling gaussian swell.
constexpr uint8_t  CFG_DEFAULT_WAVE_BASE_LEVEL       = 92;    // dim "low" — always-on baseline
constexpr uint8_t  CFG_DEFAULT_WAVE_SWELL_PEAK       = 163;   // base + peak = 255 = full at crest
constexpr uint16_t CFG_DEFAULT_WAVE_PERIOD_MS        = 7500;
constexpr uint16_t CFG_DEFAULT_LED_CROSSFADE_MS      = 1100;

// LED — hide/show fade (short-press toggle). Step per smoother tick on the
// 0..255 visibility scaler. Default 4 ≈ 640 ms 0→255.
constexpr uint8_t  CFG_DEFAULT_LED_SCALE_STEP_PER_TICK = 4;

// Mist drive
constexpr uint8_t  CFG_DEFAULT_MIST_DUTY_MAX         = 127;   // 50 % = full mist
constexpr uint8_t  CFG_DEFAULT_MIST_DUTY_MIN         = 0;     // "low" floor when level > 0
constexpr uint8_t  CFG_DEFAULT_LEVEL_DEFAULT         = 255;   // boot intensity
// Mist trough when wave is at minimum (Q8). 92/256 ≈ 36 % matches
// cfg.waveBaseLevel/255 so the mist's swing mirrors the LEDs'.
constexpr uint16_t CFG_DEFAULT_MIST_WAVE_TROUGH_Q8   = 92;

// Level smoother (drives both mist + LEDs together)
constexpr uint16_t CFG_DEFAULT_LEVEL_SMOOTH_TICK_MS      = 10;
constexpr uint8_t  CFG_DEFAULT_LEVEL_SMOOTH_STEP_UP      = 2;
constexpr uint8_t  CFG_DEFAULT_LEVEL_SMOOTH_STEP_DN      = 3;
constexpr uint8_t  CFG_DEFAULT_LEVEL_SMOOTH_STEP_UP_FAST = 4;
constexpr uint16_t CFG_DEFAULT_LEVEL_RAMP_TICK_MS        = 13;
constexpr uint8_t  CFG_DEFAULT_LEVEL_RAMP_STEP           = 1;

// Button + reed timing
constexpr uint16_t CFG_DEFAULT_BUTTON_DEBOUNCE_MS    = 50;
constexpr uint16_t CFG_DEFAULT_BUTTON_LONGPRESS_MS   = 500;
constexpr uint16_t CFG_DEFAULT_BUTTON_LONGTICK_MS    = 13;
constexpr uint16_t CFG_DEFAULT_REED_INSERT_DWELL_MS  = 500;
constexpr uint16_t CFG_DEFAULT_REED_REMOVE_DWELL_MS  = 100;

// Status LED (D7)
constexpr uint8_t  CFG_DEFAULT_STATUS_LED_DIM_DUTY   = 24;    // ~10 %

// Current-sense classifier — disc-presence + water-level detection.
// Bench numbers per David's 2026-05-15 measurements (XIAO ESP32-C6 + INA180A3
// + 30 mΩ shunt). Stored as uint16_t × 10 in NVS so 0.1 mA precision survives.
constexpr uint8_t  CFG_DEFAULT_SENSE_PROBE_DUTY              = 10;    // PWM for disc-presence probe
constexpr uint16_t CFG_DEFAULT_SENSE_DISC_PRESENT_MA10X      = 100;   // 10.0 mA — above = disc present at PWM=10
constexpr uint8_t  CFG_DEFAULT_SENSE_WATER_PROBE_DUTY        = 64;    // PWM for water-level probe
constexpr uint16_t CFG_DEFAULT_SENSE_WATER_LOW_MA10X         = 1100;  // 110.0 mA — below = water low at PWM=64
constexpr uint16_t CFG_DEFAULT_SENSE_DISC_DISCONN_MID_MA10X  = 700;   // 70.0 mA — below at PWM=64 means disc fell off
constexpr uint8_t  CFG_DEFAULT_SENSE_WATER_HYST_MA10X        = 50;    // 5.0 mA hysteresis on recovery
constexpr uint16_t CFG_DEFAULT_SENSE_WATER_CHECK_INTERVAL_S  = 60;    // seconds between water probes
constexpr uint16_t CFG_DEFAULT_SENSE_WATER_SHUTDOWN_S        = 300;   // 5 min countdown before WATER_DEPLETED
constexpr bool     CFG_DEFAULT_SENSE_USE_AS_REED             = false; // true = ignore reed switch, auto-probe instead
constexpr uint16_t CFG_DEFAULT_SENSE_AUTO_PROBE_INTERVAL_S    = 5;     // auto-probe interval in IDLE when senseUseAsReed=true

// ---------- Top-level state machine ----------
//
// Three container-driven states, plus an orthogonal `g_ledsHidden` boolean
// (short-press toggle) that fades the LED strip without touching mist.
enum class AppState : uint8_t {
  IDLE,
  RUNNING,
  TRANSITION_FROM_RUNNING,
};

// LED render mode — owned by the state machine, consumed by led_driver.ino.
enum class LedMode : uint8_t {
  BREATH,   // idle: deep dim exp(sin) breath, all 14 LEDs share brightness
  WAVE,     // docked: soft gaussian swell traveling bottom→top, all LEDs lit
};

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

// Piezo health states from the current-sense classifier (orthogonal to AppState).
// Set by piezo_sense.ino, surfaced in /api/status and the web UI status pill.
enum class PiezoState : uint8_t {
  UNKNOWN,           // before first probe (boot, idle pre-dock)
  DISC_MISSING,      // probe on dock returned current below disc-present threshold
  DISC_DRY,          // disc present at PWM=10 but water-level probe says dry (rare)
  WATER_OK,          // last water probe was above threshold — normal operation
  WATER_LOW,         // last water probe below threshold — countdown active
  WATER_DEPLETED,    // countdown expired — mist hard-stopped, awaiting container lift
  DISC_DISCONNECTED, // disc snapped off mid-run — hard-stopped immediately
};
