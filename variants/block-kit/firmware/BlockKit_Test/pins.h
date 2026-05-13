// Block Kit V0.1 — single source of truth for pins, constants, and tunables.
// Every `.ino` reads from here; never duplicate a magic number elsewhere.

#pragma once
#include <Arduino.h>

// ---------- Board portability shim ----------
// The Block Kit V0.1 PCB physically only mates with a XIAO socket, but we keep
// the sketch buildable on other ESP32 dev boards (e.g. Adafruit QT Py ESP32-S3)
// so cross-compilation / porting work doesn't require gutting the pin map.
// If the board variant doesn't define the XIAO-style `Dx` labels, fall back to
// safe per-board GPIO numbers. These fallbacks are NOT the real Block Kit
// pinout — they only ensure `pinMode`/`digitalRead`/`ledcAttach` calls resolve.
#if !defined(D0)
  // QT Py ESP32-S3 has TX=5, RX=16, SDA=7, SCL=6, A0=18, A1=17, A2=9, A3=8.
  // We pick GPIOs that don't conflict with the I2C bus the IS31FL3731 needs.
  #define D0  5    // TX  — mist PWM
  #define D1  16   // RX  — (unused on Block Kit firmware)
  #define D2  18   // A0  — INA180 ADC
  #define D3  8    // A3  — boost EN
  #define D4  SDA  // board default SDA
  #define D5  SCL  // board default SCL
  #define D6  9    // A2  — button
  #define D7  17   // A1  — status LED
  #define D8  35   // MOSI — spare
  #define D9  37   // MISO — spare
  #define D10 36   // SCK  — reed
#endif

// ---------- GPIO assignments (XIAO ESP32-C6, Block Kit V0.1 schematic) ----------
constexpr uint8_t PIN_MIST_PWM     = D0;   // 108.7 kHz PWM to MOSFET gate driver
constexpr uint8_t PIN_REED         = D10;  // Reed switch, INPUT_PULLUP, LOW = container present
constexpr uint8_t PIN_CURRENT_ADC  = D2;   // INA180A3 output via 1k+1uF filter
constexpr uint8_t PIN_BOOST_EN     = D3;   // TPS61023 enable (HIGH = 5V rail on); also lights red LED2
constexpr uint8_t PIN_BUTTON       = D6;   // Active-HIGH momentary, 10k pull-down on PCB
constexpr uint8_t PIN_STATUS_LED   = D7;   // White LED, breathing in idle

// ---------- Mist PWM ----------
constexpr uint32_t MIST_FREQ_HZ    = 108700;  // ceramic disc resonance
constexpr uint8_t  MIST_PWM_RES    = 8;       // 8-bit -> 0..255 duty
constexpr uint8_t  MIST_DUTY_RUN   = 127;     // 50% duty when running

// ---------- Status LED (D7) breathing ----------
constexpr uint32_t STATUS_LED_FREQ_HZ = 5000;
constexpr uint8_t  STATUS_LED_RES     = 8;
constexpr uint32_t STATUS_LED_BREATH_PERIOD_MS = 3000;
constexpr uint8_t  STATUS_LED_BREATH_MAX = 220;  // peak duty during breath

// ---------- INA180 current sense ----------
// INA180A3 gain = 100 V/V, R_sense = 30 mOhm  =>  V_out = I * 3.0 V/A
// => mA = adc_volts * 1000 / 3.0
constexpr float CURRENT_SENSE_FACTOR = 3.0f;        // V per A
constexpr uint16_t CURRENT_SAMPLE_HZ = 1000;        // ADC sample rate target
constexpr uint16_t CURRENT_WINDOW_N  = 256;         // sliding window length
constexpr uint32_t SCOPE_PRINT_HZ    = 100;         // scope-mode print rate

// ---------- Button (D6) timing ----------
constexpr uint16_t BUTTON_DEBOUNCE_MS    = 50;
constexpr uint16_t BUTTON_LONGPRESS_MS   = 500;
constexpr uint16_t BUTTON_LONGTICK_MS    = 13;      // ~80 steps/sec while held

// ---------- Reed switch (D10) timing ----------
constexpr uint16_t REED_INSERT_DWELL_MS  = 500;     // require stable LOW for 500 ms before auto-start
constexpr uint16_t REED_REMOVE_DWELL_MS  = 100;     // faster removal so shut-off isn't sluggish

// ---------- IS31FL3731 swirl animation ----------
constexpr uint8_t  LED_COUNT          = 14;
constexpr uint8_t  LED_IS31_ADDR      = 0x74;
constexpr uint8_t  LED_DEFAULT_OVERALL  = 60;       // resting brightness 0..255
constexpr uint8_t  LED_DEFAULT_CONTRAST = 12;       // wave amplitude 0..64
constexpr uint16_t LED_DEFAULT_PERIOD_MS = 6000;    // wave period
constexpr uint16_t LED_TICK_MS        = 30;         // ~33 fps
constexpr uint16_t LED_DIM_RAMP_TICK_MS = 13;       // long-press LED dim step rate
constexpr uint8_t  LED_DIM_STEP        = 1;         // overall delta per dim tick
constexpr uint8_t  LED_DIM_OFF_THRESHOLD = 8;       // overall <= this clamps to 0

// 14-LED (x,y) positions on the IS31FL3731 Matrix B frame buffer.
// First guess from the schematic's CB1..CB9 wiring; verify with `ledWalk()`
// (Serial command `w`) at bring-up and reorder so index 0 = top, 13 = bottom.
constexpr uint8_t LED_POSITIONS[LED_COUNT][2] = {
  // Row 1 (CB1 anode -> CB2..CB9 cathodes): 8 LEDs
  {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0},
  // Row 2 (CB2 anode -> CB3..CB8 cathodes): 6 LEDs
  {0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1},
};

// ---------- Serial ----------
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t PLOT_PRINT_HZ = 20;              // [PLOT] CSV print rate when scope mode off

// ---------- Top-level state machine ----------
enum class AppState : uint8_t {
  IDLE,       // no container or user override off; boost rail down (D3 LOW)
  RUNNING,    // mist active
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
