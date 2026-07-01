// Mist Maker Battery Kit V0.4 — pin map + hardware constants.
// (D0–D7 are identical on V0.3; V0.4 adds the power-mux status sense on D8.)
//
// Single-PCB portable variant: LiPo + USB-C charging (LP4060), TPS2116 power
// mux (USB-priority), TPS61023 boost for the piezo rail, INA180A3 current
// sense, button, status LED, a resistor divider on D1 to read the battery,
// and (new in V0.4) the TPS2116 STATUS pin on D8 so firmware can tell whether
// the load is running from USB or from the cell.
//
// KiCad nets -> pins (MistMaker-Battery-Kit-V0-4.kicad_sch):
//   MIST_PWM_3V3    -> D0   108.7 kHz PWM to the gate driver
//   D1_BATT_VOLTAGE -> D1   battery via 10k/10k divider (ratio 2.0)
//   D2_CS           -> D2   INA180A3 current-sense output
//   D3_TPS_EN       -> D3   TPS61023 EN (HIGH = ~5 V boost rail on)
//   D4_SDA / D5_SCL -> D4/D5  I2C (Qwiic)
//   D6_BUTTON       -> D6   active-HIGH, PCB 10k pull-down
//   D7_LED          -> D7   status LED
//   D8_ST           -> D8   TPS2116 STATUS (V0.4+): HIGH = on USB, LOW = on cell
//   D9/D10          ->      spare breakout

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
  // TPS2116 STATUS pin, level-shifted through R21/R22 onto net D8_ST (V0.4+).
  //   HIGH (~2.6 V) -> mux sourcing VIN1 = USB present; the cell is charging.
  //   LOW  (~0 V)   -> mux sourcing VIN2 = running on the cell; Vbatt = true SoC.
  // Plain INPUT only: the external divider defines the level; an internal
  // pull-up/down would swamp it and flip the reading. (On V0.3, D8 is an
  // unconnected spare, so the USB/mux test is V0.4+ only.)
  constexpr uint8_t PIN_USB_SENSE   = D8;
#else
  #error "Battery Kit V0.3 firmware: select a XIAO ESP32 board in Tools > Board."
#endif

// Piezo drive
constexpr uint32_t MIST_FREQ_HZ   = 108700;
constexpr uint8_t  MIST_PWM_RES   = 8;
constexpr uint8_t  MIST_DUTY_FULL = 127;     // 50% — resonant sweet spot

// INA180A3 current sense: V per A = gain (100 V/V) x shunt (30 mOhm)
constexpr float CURRENT_SENSE_FACTOR = 3.0f;

// Battery divider: Vbatt = Vpin x ratio. V0.3/V0.4 use equal 10k/10k -> 2.0.
// The battery read uses analogReadMilliVolts() for the C6's calibrated ADC
// (raw analogRead is nonlinear on C6 v3.x — arduino-esp32 #11324).
constexpr float BATT_DIVIDER_RATIO = 2.0f;

// LiPo guidance (under load): warn below LOW, shut down below CRITICAL.
constexpr float BATT_LOW_V      = 3.45f;
constexpr float BATT_CRITICAL_V = 3.20f;

constexpr uint16_t BUTTON_DEBOUNCE_MS = 50;
constexpr uint32_t SERIAL_BAUD = 115200;
