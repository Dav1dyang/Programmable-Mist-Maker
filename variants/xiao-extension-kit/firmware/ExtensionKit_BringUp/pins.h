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

// The Extension has no button of its own, so the self-test trigger is the
// XIAO's BOOT button (active LOW, safe to read after boot). Its GPIO differs
// per SoC — raw GPIO numbers, not Dn labels (on the C6, BOOT is GPIO9 but
// D9 is GPIO20).
#if defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32C3)
  constexpr uint8_t PIN_BOOT_BTN = 9;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  constexpr uint8_t PIN_BOOT_BTN = 0;
#endif

// Piezo drive
constexpr uint32_t MIST_FREQ_HZ   = 108700;  // ceramic disc resonance
constexpr uint8_t  MIST_PWM_RES   = 8;
constexpr uint8_t  MIST_DUTY_FULL = 127;     // 50% — resonant sweet spot

// INA180A3 current sense: V per A = gain (100 V/V) x shunt (30 mOhm)
constexpr float CURRENT_SENSE_FACTOR = 3.0f;

constexpr uint32_t SERIAL_BAUD = 115200;

// ---------------------------------------------------------------------------
// Automated self-test limits (press BOOT, or `a` over serial).
// Provenance: same measured bands as the Battery Kit acceptance protocol
// (V04-Acceptance-Test_2026-07-04.md §C) — both boards share the identical
// drive + sense chain, and the bands were taken with this sketch's raw-ADC
// mA conversion at the default 50% duty. One Extension-specific note: its
// INA180 runs from 3V3, so readings clip near ~1.07 A (irrelevant at 50%
// duty, matters only in sweeps).
// The bands are duplicated in BatteryKit_BringUp/pins.h by design —
// standalone sketches, same physical truth. Re-characterize? Edit BOTH
// pins.h files together.
// ---------------------------------------------------------------------------
constexpr uint16_t ST_BTN_WAIT_MS = 5000;   // BOOT-press window (serial start)

constexpr float ST_IDLE_MAX_MA  = 10.0f;    // PWM off: INA180 zero + leakage
constexpr float ST_LOAD_MIN_MA  = 10.0f;    // below at full drive -> no disc
constexpr float ST_DRY_MIN_MA   = 60.0f;    // 60-115 = dry-disc band
constexpr float ST_WATER_MIN_MA = 115.0f;   // 115-280 = disc-in-water band
constexpr float ST_LOAD_MAX_MA  = 280.0f;   // above at 50% duty -> investigate

// Row results. Lives here (not the .ino) so the Arduino builder's auto-hoisted
// prototypes see the type. skip = couldn't run, info = no pass criterion,
// CHECK = needs the operator's eyes. Only FAIL rows fail the verdict.
enum TestResult : uint8_t { T_PASS, T_FAIL, T_SKIP, T_INFO, T_CHECK };
