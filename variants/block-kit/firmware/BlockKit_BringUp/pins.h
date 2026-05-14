// Block Kit V0.1 — BringUp sketch pin map.
// Hardware constants only. No state-machine tunables; the BringUp sketch is
// deliberately dumb. For the full production tunables see
// ../BlockKit_Production/pins.h.

#pragma once
#include <Arduino.h>

#if defined(ARDUINO_XIAO_ESP32C6) || \
    defined(ARDUINO_XIAO_ESP32S3) || \
    defined(ARDUINO_XIAO_ESP32S3_PLUS)

  constexpr uint8_t PIN_MIST_PWM     = D0;   // 108.7 kHz PWM to MOSFET gate
  constexpr uint8_t PIN_REED         = D10;  // Reed switch (INPUT_PULLUP), LOW=docked
  constexpr uint8_t PIN_CURRENT_ADC  = D2;   // INA180A3 via 1k+1uF filter
  constexpr uint8_t PIN_BOOST_EN     = D3;   // TPS61023 EN (HIGH = 5V rail on)
  constexpr uint8_t PIN_BUTTON       = D6;   // Active-HIGH, PCB 10k pull-down
  constexpr uint8_t PIN_STATUS_LED   = D7;   // White LED

#elif defined(ARDUINO_ADAFRUIT_QTPY_ESP32S3_NOPSRAM) || \
      defined(ARDUINO_ADAFRUIT_QTPY_ESP32S3_N4R2)    || \
      defined(ARDUINO_ADAFRUIT_QTPY_ESP32S2)         || \
      defined(ARDUINO_ADAFRUIT_QTPY_ESP32C3)         || \
      defined(ARDUINO_ADAFRUIT_QTPY_ESP32_PICO)

  // Compile-only port; QT Py doesn't physically mate with the Block Kit PCB.
  constexpr uint8_t PIN_MIST_PWM     = TX;
  constexpr uint8_t PIN_REED         = RX;
  constexpr uint8_t PIN_CURRENT_ADC  = A0;
  constexpr uint8_t PIN_BOOST_EN     = A3;
  constexpr uint8_t PIN_BUTTON       = A2;
  constexpr uint8_t PIN_STATUS_LED   = A1;

#else
  #error "Block Kit V0.1 BringUp: select a supported board in Arduino IDE \
> Tools > Board. Supported: XIAO_ESP32C6 (production target), XIAO_ESP32S3, \
XIAO_ESP32S3_PLUS, or any Adafruit QT Py ESP32 variant (compile-only)."
#endif

// ---- Mist PWM ----
constexpr uint32_t MIST_FREQ_HZ      = 108700;
constexpr uint8_t  MIST_PWM_RES      = 8;       // 8-bit -> 0..255 duty
constexpr uint8_t  MIST_DUTY_FULL    = 127;     // 50% duty = full mist

// ---- IS31FL3731 LED strip (14 LEDs, vertical) ----
constexpr uint8_t  LED_COUNT         = 14;
constexpr uint8_t  LED_IS31_ADDR     = 0x74;
constexpr uint8_t  LED_IS31_FRAME    = 0;
constexpr uint8_t  LED_FIXED_ON_PWM  = 80;      // bright-enough sanity test
constexpr uint8_t  LED_WALK_PWM      = 200;     // chase brightness

// LED numbers on IS31FL3731 Matrix B, ordered top (index 0) to bottom (13).
constexpr uint8_t LED_MAP[LED_COUNT] = {
   8,  9, 10, 11, 12, 13, 14, 15,   // CB1 row: top, LEDs 1..8
  24, 25, 26, 27, 28, 29,           // CB2 row: LEDs 9..14 (bottom)
};

// ---- INA180A3 current sense ----
// INA180A3 gain = 100 V/V, R_sense = 30 mOhm  ->  V_out = I * 3.0 V/A
constexpr float    CURRENT_SENSE_FACTOR = 3.0f;

// ---- Status LED (D7) ----
constexpr uint32_t STATUS_LED_FREQ_HZ   = 5000;
constexpr uint8_t  STATUS_LED_RES       = 8;
constexpr uint8_t  STATUS_LED_DIM_DUTY  = 24;   // ~10 % when idle

// ---- Button (D6) ----
constexpr uint16_t BUTTON_DEBOUNCE_MS   = 50;

// ---- Serial ----
constexpr uint32_t SERIAL_BAUD          = 115200;
