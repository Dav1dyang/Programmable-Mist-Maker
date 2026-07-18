// Mist Maker Battery Kit V0.4 / V0.4.1 — pin map + hardware constants.
// (D0–D7 are identical on V0.3; V0.4 adds the power-mux status sense on D8.
//  V0.4.1 — the July 2026 production spin — keeps the same pin map; what
//  changes is passive: R22 150k->220k stiffens D8's USB-high to ~3.1 V, and
//  R8 flips to a pull-DOWN so the ~5 V boost rail is DEAD at power-on until
//  firmware raises D3. Same firmware runs on all three revisions.)
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
  //   HIGH -> mux sourcing VIN1 = USB present; the cell is charging.
  //          (~2.6 V on V0.4 with R22=150k; ~3.1 V on V0.4.1 with R22=220k —
  //           the spin widened the logic-high margin from 0.2 V to 0.66 V.)
  //   LOW  (~0 V) -> mux sourcing VIN2 = running on the cell; Vbatt = true SoC.
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

// ---------------------------------------------------------------------------
// Automated self-test limits (long-press the button >= 1.5 s, or `a`).
// Provenance: ../../hardware/V04-Acceptance-Test_2026-07-04.md — the bands
// below are that protocol's measured pass windows,
// taken at the default 50% duty with THIS sketch's raw-ADC mA conversion.
// Deliberately NOT recalibrated to analogReadMilliVolts(): the bands and the
// conversion were measured together; changing one invalidates the other.
// The current bands are the same physical truth as the Extension Kit's (same
// disc, same drive + sense chain) and are duplicated there by design —
// standalone sketches. Re-characterize? Edit BOTH pins.h files together:
// this one and ExtensionKit_BringUp/pins.h.
// ---------------------------------------------------------------------------
constexpr uint16_t BTN_LONGPRESS_MS = 1500;  // hold this long -> self-test
constexpr uint16_t ST_BTN_WAIT_MS   = 5000;  // button-test window (serial start)

// Row results. Lives here (not the .ino) so the Arduino builder's auto-hoisted
// prototypes see the type. skip = couldn't run, info = no pass criterion,
// CHECK = needs the operator's eyes. Only FAIL rows fail the verdict.
enum TestResult : uint8_t { T_PASS, T_FAIL, T_SKIP, T_INFO, T_CHECK };

// Battery divider window. Covers a real cell (3.0-4.2 V) and the no-cell case
// on USB (divider tracks the charger output, ~4.1-4.2 V). Near-zero = divider
// open / ADC dead; above max = divider ratio wrong (check R18/R19).
constexpr float ST_VBAT_MIN_V  = 2.80f;
constexpr float ST_VBAT_MAX_V  = 4.40f;

// Current bands at 50% duty (protocol §C: water 130-200 mA, dry 70-100 mA,
// unplugged 0-10 mA; duty-sweep plateau reaches ~225 mA on a healthy V0.4).
// Margins widened for water-level / disc spread across production boards.
constexpr float ST_IDLE_MAX_MA  = 10.0f;   // rail off: INA180 zero + leakage
constexpr float ST_LOAD_MIN_MA  = 10.0f;   // below at full drive -> no disc
constexpr float ST_DRY_MIN_MA   = 60.0f;   // 60-115 = dry-disc band
constexpr float ST_WATER_MIN_MA = 115.0f;  // 115-450 = disc-in-water band
// Water-band ceiling: coupling varies with water level/disc seating — a
// well-coupled disc measured a stable ~372 mA WITH a strong plume (V0.4.1
// bench, 2026-07-18), vs ~180 mA lighter-coupled minutes earlier on the
// same board. Real drive-stage faults live much higher (sweep: 480 mA only
// at 62% duty, ~1.1 A at 75%), so the fault line sits above the healthy
// coupling range, not at the reference-bench 130-225 plateau.
constexpr float ST_LOAD_MAX_MA  = 450.0f;  // above at 50% duty -> investigate
