// Mist Maker Battery Kit V0.4 — BringUp sketch.
// (Pins D0–D7 are identical on V0.3; the `u` USB/mux test needs V0.4.)
//
// Bare-bones per-feature verification, no library required. Flash this on a
// freshly assembled board and walk the checklist:
//   1. "does the button work?"            -> press it: mist + LED toggle
//   2. "does the boost rail come up?"     -> `t`, scope the 5 V rail or watch mist
//   3. "is the INA180 reading anything?"  -> `c` prints one current reading
//   4. "is the mux ST sense alive?"       -> `u` reads USB present (D8 HIGH);
//                                            meter TP3 for the cell/LOW case
//   5. "does the battery divider read?"   -> `b` prints volts + validity
//   6. "dry vs wet separation?"           -> `s` streams CSV for the Plotter
//   7. "does dimming work?"               -> `0`..`9` sets duty
//   8. "does the mux hand over cleanly?"  -> cell in, mist on, unplug USB,
//      replug, press `u`: the flip counter shows the USB->BATT->USB handoff
//      happened, and a boot reason still reading "power-on" proves the
//      TPS2116's ~1.3 ms break-before-make gap didn't brown out the MCU.
//
// The TPS2116 power mux (V0.4) runs the board off USB whenever it's plugged
// in, so Vbatt reflects true state-of-charge only on the cell. D8 (mux ST)
// tells us which: HIGH = USB, LOW = battery. `b`/`[STAT]` always print the
// voltage + OK/LOW/CRITICAL tag and add "[USB - charging]" on USB (where the
// number tracks the charger, not SoC).
//
// NOTE: on this board USB is the only serial link, so over the console D8
// almost always reads HIGH (USB). To confirm the LOW/cell state, meter TP3
// (or D8) with the cell in and USB out — you can't watch it flip over serial.
//
// Expected (XIAO C6, INA180A3, 30 mOhm shunt, duty 64):
//   no disc ~0 mA · disc dry ~70-100 mA · disc in water ~130-200 mA
//   D8: on USB reads HIGH (~2.6 V), on battery reads LOW (~0 V)
// Battery: 4.2 V full · 3.7 V nominal · <3.45 V low · <3.20 V critical
//
// For real applications use the MistMaker library (>= 1.2.0) instead:
// https://github.com/owochel/MistMaker

#include "pins.h"
#include <esp_system.h>   // esp_reset_reason(): brownout vs power-on at boot

static bool     g_on        = false;
static uint8_t  g_duty      = MIST_DUTY_FULL;
static bool     g_scope     = false;
static bool     g_btnDeb    = false;
static bool     g_btnRaw    = false;
static uint32_t g_btnEdgeMs = 0;
static uint32_t g_lastStatMs  = 0;
static uint32_t g_lastScopeMs = 0;

// Power-source handoff bookkeeping (survives USB unplug, unlike serial output):
// the mux switches USB<->cell in ~1.3 ms with the serial link down, so we count
// flips here and let `u` report them after the fact.
static bool     g_lastUsb    = false;
static uint16_t g_usbFlips   = 0;
static uint32_t g_lastFlipMs = 0;

static const char* resetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";   // mux handoff gap too deep
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_WDT: case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT:
                            return "watchdog";
    default:                return "other";
  }
}

static inline float adcToMa(uint16_t raw) {
  const float volts = (float(raw) * 3.3f) / 4095.0f;
  return (volts * 1000.0f) / CURRENT_SENSE_FACTOR;
}

static float readMa(uint16_t sampleMs = 50) {
  uint32_t sum = 0, n = 0;
  const uint32_t start = millis();
  while (millis() - start < sampleMs) { sum += analogRead(PIN_CURRENT_ADC); n++; }
  return n ? adcToMa(uint16_t(sum / n)) : 0.0f;
}

static float readBatteryVolts() {
  // analogReadMilliVolts() applies the C6's factory eFuse calibration, so the
  // reading is linear millivolts (raw analogRead is nonlinear — #11324).
  uint32_t sum_mV = 0;
  for (uint8_t i = 0; i < 16; i++) sum_mV += analogReadMilliVolts(PIN_BATT_ADC);
  return (float(sum_mV) / 16.0f) / 1000.0f * BATT_DIVIDER_RATIO;
}

// Mux status on D8 (V0.4+): HIGH = load on USB (VIN1), LOW = load on the cell
// (VIN2). Vbatt is a trustworthy state-of-charge only when this is false.
static inline bool onUsbPower() {
  return digitalRead(PIN_USB_SENSE) == HIGH;
}

static void applyOutput() {
  if (g_on) {
    digitalWrite(PIN_BOOST_EN, HIGH);
    delayMicroseconds(500);                       // 5V5 rail settle
    ledcWrite(PIN_MIST_PWM, g_duty);
    digitalWrite(PIN_STATUS_LED, HIGH);
    Serial.printf("[OUT] ON  boost=HIGH duty=%u led=ON\n", g_duty);
  } else {
    ledcWrite(PIN_MIST_PWM, 0);
    digitalWrite(PIN_MIST_PWM, LOW);              // safety belt
    digitalWrite(PIN_BOOST_EN, LOW);
    digitalWrite(PIN_STATUS_LED, LOW);
    Serial.println("[OUT] OFF boost=LOW  duty=0 led=off");
  }
}

static void printHelp() {
  Serial.println("[HELP] button or t=toggle  c=current  b=battery  u=usb/mux  s=scope");
  Serial.println("[HELP] 0..9 = output 0..90% of full drive (full = the 50%-duty sweet spot)");
  Serial.println("[HELP] handoff test: cell in -> unplug USB -> replug -> u");
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  pinMode(PIN_CURRENT_ADC, INPUT);   // raw analogRead for current sense; do NOT
  pinMode(PIN_BATT_ADC, INPUT);      // call analogReadResolution() on C6 (v3.x
                                     // bug) — battery uses analogReadMilliVolts.
  pinMode(PIN_BUTTON, INPUT);        // PCB has its own 10k pull-down
  pinMode(PIN_USB_SENSE, INPUT);     // mux ST via external divider — no pull!
  pinMode(PIN_BOOST_EN, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  // R8 pulls EN up to the mux rail, so the ~5 V boost rail is LIVE from
  // power-on until this line runs (the piezo stays off — R11 holds Q4's gate
  // down). Expected if you're scoping the rail; not a fault.
  digitalWrite(PIN_BOOST_EN, LOW);

  ledcAttach(PIN_MIST_PWM, MIST_FREQ_HZ, MIST_PWM_RES);
  ledcWrite(PIN_MIST_PWM, 0);

  Serial.println("==============================================");
  Serial.println(" Battery Kit V0.4 - BringUp");
  Serial.println("==============================================");
  Serial.printf("[BOOT] reset: %s\n", resetReasonStr());
  g_lastUsb = onUsbPower();
  Serial.printf("[USB] boot source: %s\n",
                g_lastUsb ? "USB (VIN1)" : "battery (VIN2)");
  Serial.printf("[BATT] %.2f V at boot\n", readBatteryVolts());
  printHelp();
}

void loop() {
  // ---- button (active HIGH, debounced) ----
  const bool raw = digitalRead(PIN_BUTTON) == HIGH;
  if (raw != g_btnRaw) { g_btnRaw = raw; g_btnEdgeMs = millis(); }
  if (millis() - g_btnEdgeMs > BUTTON_DEBOUNCE_MS && g_btnDeb != g_btnRaw) {
    g_btnDeb = g_btnRaw;
    if (g_btnDeb) { g_on = !g_on; applyOutput(); }   // rising edge = toggle
  }

  // ---- power-source handoff counter (ST settles in us; 50 ms rate-limit) ----
  const bool usbNow = onUsbPower();
  if (usbNow != g_lastUsb && millis() - g_lastFlipMs > 50) {
    g_lastUsb = usbNow;
    g_usbFlips++;
    g_lastFlipMs = millis();
    // Often unseen live (unplugging USB drops serial) — `u` replays it.
    Serial.printf("[USB] source now %s (handoff #%u)\n",
                  usbNow ? "USB" : "BATTERY", g_usbFlips);
  }

  // ---- serial commands ----
  while (Serial.available()) {
    const char ch = Serial.read();
    if (ch == 't')      { g_on = !g_on; applyOutput(); }
    else if (ch == 'c') { Serial.printf("[CUR] %.1f mA\n", readMa()); }
    else if (ch == 'b') {
      const float v = readBatteryVolts();
      const char* tag = v < BATT_CRITICAL_V ? "CRITICAL"
                       : v < BATT_LOW_V     ? "LOW" : "OK";
      // Always show the tag — USB is the serial link, so the bench is always on
      // USB — and flag it: on USB the divider reads the charger, not SoC.
      Serial.printf("[BATT] %.2f V (%s)%s\n", v, tag,
                    onUsbPower() ? "  [USB - charging, SoC N/A]" : "");
    }
    else if (ch == 'u') {
      const bool usb = onUsbPower();
      Serial.printf("[USB] %s  ->  battery reading is %s\n",
                    usb ? "present : mux on VIN1 (USB)"
                        : "absent  : mux on VIN2 (battery)",
                    usb ? "charging / not SoC" : "a valid SoC");
      if (g_usbFlips) {
        Serial.printf("[USB] handoffs since boot: %u (last at %lu ms); "
                      "boot reset was \"%s\" -> %s\n",
                      g_usbFlips, (unsigned long)g_lastFlipMs, resetReasonStr(),
                      esp_reset_reason() == ESP_RST_BROWNOUT
                          ? "handoff BROWNED OUT the MCU"
                          : "handoff survived, no reset");
      } else {
        Serial.println("[USB] no handoffs seen yet - with the cell in, unplug "
                       "and replug USB, then press u again");
      }
    }
    else if (ch == 's') { g_scope = !g_scope;
                          Serial.printf("[CUR] scope %s\n", g_scope ? "ON" : "OFF"); }
    else if (ch == 'h') { printHelp(); }
    else if (ch >= '0' && ch <= '9') {
      g_duty = uint8_t((ch - '0') * MIST_DUTY_FULL / 10);
      if (g_on) applyOutput();
      Serial.printf("[CFG] duty=%u\n", g_duty);
    }
  }

  // ---- scope stream for Serial Plotter ----
  if (g_scope && millis() - g_lastScopeMs >= 10) {
    g_lastScopeMs = millis();
    Serial.printf("mA:%.1f\tVbatt:%.2f\tUSB:%d\n",
                  adcToMa(analogRead(PIN_CURRENT_ADC)), readBatteryVolts(),
                  onUsbPower() ? 1 : 0);
  }

  // ---- periodic status ----
  if (!g_scope && millis() - g_lastStatMs >= 2000) {
    g_lastStatMs = millis();
    const bool usb = onUsbPower();
    const float v  = readBatteryVolts();
    const char* batt = v < BATT_CRITICAL_V ? "CRIT" : v < BATT_LOW_V ? "low" : "ok";
    Serial.printf("[STAT] mist=%s duty=%u current=%.1f mA src=%s batt=%.2f V (%s)\n",
                  g_on ? "ON" : "off", g_on ? g_duty : 0,
                  readMa(20), usb ? "USB" : "BATT", v, batt);
  }
}
