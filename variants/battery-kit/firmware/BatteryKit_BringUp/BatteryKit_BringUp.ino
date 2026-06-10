// Mist Maker Battery Kit V0.3 — BringUp sketch.
//
// Bare-bones per-feature verification, no library required. Flash this on a
// freshly assembled board and walk the checklist:
//   1. "does the button work?"            -> press it: mist + LED toggle
//   2. "does the boost rail come up?"     -> `t`, scope 5V5 or watch mist
//   3. "is the INA180 reading anything?"  -> `c` prints one current reading
//   4. "does the battery divider read?"   -> `b` prints volts (USB unplugged
//                                            for a true battery reading)
//   5. "dry vs wet separation?"           -> `s` streams CSV for the Plotter
//   6. "does dimming work?"               -> `0`..`9` sets duty
//
// Expected (XIAO C6, INA180A3, 30 mOhm shunt, duty 64):
//   no disc ~0 mA · disc dry ~70-100 mA · disc in water ~130-200 mA
// Battery: 4.2 V full · 3.7 V nominal · <3.45 V low · <3.20 V critical
//
// For real applications use the MistMaker library (>= 1.1.0) instead:
// https://github.com/owochel/MistMaker

#include "pins.h"

static bool     g_on        = false;
static uint8_t  g_duty      = MIST_DUTY_FULL;
static bool     g_scope     = false;
static bool     g_btnDeb    = false;
static bool     g_btnRaw    = false;
static uint32_t g_btnEdgeMs = 0;
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

static float readBatteryVolts() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 16; i++) sum += analogRead(PIN_BATT_ADC);
  return (float(sum) / 16.0f) * 3.3f / 4095.0f * BATT_DIVIDER_RATIO;
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
  Serial.println("[HELP] button or t=toggle  c=current  b=battery  s=scope");
  Serial.println("[HELP] 0..9 = duty 0..90%  h=help");
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  pinMode(PIN_CURRENT_ADC, INPUT);   // core ADC defaults — do NOT call
  pinMode(PIN_BATT_ADC, INPUT);      // analogReadResolution() on C6 (v3.x bug)
  pinMode(PIN_BUTTON, INPUT);        // PCB has its own 10k pull-down
  pinMode(PIN_BOOST_EN, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_BOOST_EN, LOW);

  ledcAttach(PIN_MIST_PWM, MIST_FREQ_HZ, MIST_PWM_RES);
  ledcWrite(PIN_MIST_PWM, 0);

  Serial.println("==============================================");
  Serial.println(" Battery Kit V0.3 - BringUp");
  Serial.println("==============================================");
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

  // ---- serial commands ----
  while (Serial.available()) {
    const char ch = Serial.read();
    if (ch == 't')      { g_on = !g_on; applyOutput(); }
    else if (ch == 'c') { Serial.printf("[CUR] %.1f mA\n", readMa()); }
    else if (ch == 'b') {
      const float v = readBatteryVolts();
      const char* tag = v < BATT_CRITICAL_V ? "CRITICAL"
                       : v < BATT_LOW_V     ? "LOW" : "OK";
      Serial.printf("[BATT] %.2f V (%s)\n", v, tag);
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
    Serial.printf("mA:%.1f\tVbatt:%.2f\n",
                  adcToMa(analogRead(PIN_CURRENT_ADC)), readBatteryVolts());
  }

  // ---- periodic status ----
  if (!g_scope && millis() - g_lastStatMs >= 2000) {
    g_lastStatMs = millis();
    Serial.printf("[STAT] mist=%s duty=%u current=%.1f mA batt=%.2f V\n",
                  g_on ? "ON" : "off", g_on ? g_duty : 0,
                  readMa(20), readBatteryVolts());
  }
}
