// Mist Maker Extension V0.1 (MistMaker-Seeed-Expansion-V0.1) — pin map.
//
// The Extension is a XIAO add-on board: piezo drive stage (UCC27511 gate
// driver + DMT10H009 MOSFET + 3-legged inductor) and an INA180A3 analog
// current sense. Power comes straight from the XIAO's USB-C 5 V — no boost
// converter, no battery on this variant.
//
// KiCad nets -> pins (MistMaker-Seeed-Expansion-V0.1.kicad_sch):
//   MIST_PWM_3V3 -> D0   108.7 kHz PWM to the gate driver
//   CS           -> D2   INA180A3 current-sense output
//   SDA / SCL    -> D4/D5  I2C breakout (free for your sensors)

#pragma once
#include <Arduino.h>

#if defined(ARDUINO_XIAO_ESP32C6) || defined(ARDUINO_XIAO_ESP32S3) || \
    defined(ARDUINO_XIAO_ESP32S3_PLUS) || defined(ARDUINO_XIAO_ESP32C3)
  constexpr uint8_t PIN_MIST_PWM    = D0;
  constexpr uint8_t PIN_CURRENT_ADC = D2;
#else
  #error "Extension Kit V0.1 firmware: select a XIAO ESP32 board in Tools > Board."
#endif

// Piezo drive
constexpr uint32_t MIST_FREQ_HZ   = 108700;  // ceramic disc resonance
constexpr uint8_t  MIST_PWM_RES   = 8;
constexpr uint8_t  MIST_DUTY_FULL = 127;     // 50% — resonant sweet spot

// INA180A3 current sense: V per A = gain (100 V/V) x shunt (30 mOhm)
constexpr float CURRENT_SENSE_FACTOR = 3.0f;

constexpr uint32_t SERIAL_BAUD = 115200;
