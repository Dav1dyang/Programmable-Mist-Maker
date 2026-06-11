// Mist Maker Extension V0.1 — BringUp sketch.
//
// Bare-bones per-feature verification, no library required. Flash this on a
// freshly assembled board to answer, in order:
//   1. "does PWM come out of D0?"          -> `t` toggles the mist
//   2. "is the INA180 reading anything?"   -> `c` prints one current reading
//   3. "do dry vs wet discs separate?"     -> `s` streams CSV for the
//                                             Arduino Serial Plotter
//   4. "does dimming work?"                -> `0`..`9` sets duty
//
// Expected currents (XIAO C6, INA180A3, 30 mOhm shunt, duty 64):
//   no disc        ~0 mA
//   disc, dry      ~70-100 mA
//   disc in water  ~130-200 mA
//
// For real applications use the MistMaker library (>= 1.1.0) instead:
// https://github.com/owochel/MistMaker

#include "pins.h"

static bool     g_on        = false;
static uint8_t  g_duty      = MIST_DUTY_FULL;
static bool     g_scope     = false;
static uint32_t g_lastStatMs  = 0;
static uint32_t g_lastScopeMs = 0;

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

static void applyOutput() {
  if (g_on) {
    ledcWrite(PIN_MIST_PWM, g_duty);
    Serial.printf("[OUT] ON  duty=%u (%.0f%%)\n", g_duty, g_duty * 100.0 / 255.0);
  } else {
    ledcWrite(PIN_MIST_PWM, 0);
    digitalWrite(PIN_MIST_PWM, LOW);
    Serial.println("[OUT] OFF");
  }
}

static void printHelp() {
  Serial.println("[HELP] t=toggle mist  c=current reading  s=scope stream");
  Serial.println("[HELP] 0..9 = duty 0..90%  h=help");
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  pinMode(PIN_CURRENT_ADC, INPUT);   // core ADC defaults — do NOT call
                                     // analogReadResolution() on C6 (v3.x bug)
  ledcAttach(PIN_MIST_PWM, MIST_FREQ_HZ, MIST_PWM_RES);
  ledcWrite(PIN_MIST_PWM, 0);

  Serial.println("==============================================");
  Serial.println(" Extension Kit V0.1 - BringUp");
  Serial.println("==============================================");
  printHelp();
}

void loop() {
  // ---- serial commands ----
  while (Serial.available()) {
    const char ch = Serial.read();
    if (ch == 't')      { g_on = !g_on; applyOutput(); }
    else if (ch == 'c') { Serial.printf("[CUR] %.1f mA\n", readMa()); }
    else if (ch == 's') { g_scope = !g_scope;
                          Serial.printf("[CUR] scope %s\n", g_scope ? "ON" : "OFF"); }
    else if (ch == 'h') { printHelp(); }
    else if (ch >= '0' && ch <= '9') {
      // 0..9 -> 0..~114 duty (i.e. 0..90% of the 127 full-mist duty)
      g_duty = uint8_t((ch - '0') * MIST_DUTY_FULL / 10);
      if (g_on) applyOutput();
      Serial.printf("[CFG] duty=%u\n", g_duty);
    }
  }

  // ---- scope stream for Serial Plotter ----
  if (g_scope && millis() - g_lastScopeMs >= 10) {
    g_lastScopeMs = millis();
    Serial.printf("mA:%.1f\n", adcToMa(analogRead(PIN_CURRENT_ADC)));
  }

  // ---- periodic status ----
  if (!g_scope && millis() - g_lastStatMs >= 2000) {
    g_lastStatMs = millis();
    Serial.printf("[STAT] mist=%s duty=%u current=%.1f mA\n",
                  g_on ? "ON" : "off", g_on ? g_duty : 0, readMa(20));
  }
}
