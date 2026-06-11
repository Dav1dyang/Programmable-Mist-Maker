// Mist Maker Battery Kit V0.3 — pin map + hardware constants.
//
// Single-PCB portable variant: LiPo + USB-C charging (MCP73831), TPS61023
// boost for the piezo rail, INA180A3 current sense, button, status LED, and
// (new in V0.3) a resistor divider on D1 to read the battery voltage.
//
// KiCad nets -> pins (MistMaker-Battery-Kit-V0-3.kicad_sch):
//   MIST_PWM_3V3    -> D0   108.7 kHz PWM to MOSFET gate
//   D1_BATT_VOLTAGE -> D1   battery via divider (V0.3+ only)
//   D2_CS           -> D2   INA180A3 current-sense output
//   D3_TPS_EN       -> D3   TPS61023 EN (HIGH = 5.5 V rail on)
//   D4_SDA / D5_SCL -> D4/D5  I2C breakout
//   D6_BUTTON       -> D6   active-HIGH, PCB 10k pull-down
//   D7_LED          -> D7   status LED
//   D8/D9/D10       ->      spare breakout

#pragma once
#include <Arduino.h>

#if defined(ARDUINO_XIAO_ESP32C6) || defined(ARDUINO_XIAO_ESP32S3) || \
    defined(ARDUINO_XIAO_ESP32S3_PLUS) || defined(ARDUINO_XIAO_ESP32C3)
  constexpr uint8_t PIN_MIST_PWM    = D0;
  constexpr uint8_t PIN_BATT_ADC    = D1;
  constexpr uint8_t PIN_CURRENT_ADC = D2;
  constexpr uint8_t PIN_BOOST_EN    = D3;
  constexpr uint8_t PIN_BUTTON      = D6;
  constexpr uint8_t PIN_STATUS_LED  = D7;
#else
  #error "Battery Kit V0.3 firmware: select a XIAO ESP32 board in Tools > Board."
#endif

// Piezo drive
constexpr uint32_t MIST_FREQ_HZ   = 108700;
constexpr uint8_t  MIST_PWM_RES   = 8;
constexpr uint8_t  MIST_DUTY_FULL = 127;     // 50% — resonant sweet spot

// INA180A3 current sense: V per A = gain (100 V/V) x shunt (30 mOhm)
constexpr float CURRENT_SENSE_FACTOR = 3.0f;

// Battery divider: Vbatt = Vpin x ratio. V0.3 uses equal resistors -> 2.0.
constexpr float BATT_DIVIDER_RATIO = 2.0f;

// LiPo guidance (under load): warn below LOW, shut down below CRITICAL.
constexpr float BATT_LOW_V      = 3.45f;
constexpr float BATT_CRITICAL_V = 3.20f;

constexpr uint16_t BUTTON_DEBOUNCE_MS = 50;
constexpr uint32_t SERIAL_BAUD = 115200;
