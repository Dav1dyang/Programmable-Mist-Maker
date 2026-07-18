// Mist Maker Extension V0.1 — BringUp sketch.
//
// Bare-bones per-feature verification, no library required. Flash this on a
// freshly assembled board, then EITHER press the XIAO's BOOT button (or send
// `a`) for the AUTOMATED SELF-TEST — a few-second pass/fail checkout — or
// answer the questions by hand, in order:
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
// For real applications use the MistMaker library (>= 2.1.0) instead:
// https://github.com/owochel/MistMaker

#include "pins.h"
#include <esp_system.h>   // esp_reset_reason() for the self-test boot check

static const char FW_VERSION[] = "1.1.0";   // BringUp sketch, not the library

static bool     g_on        = false;
static uint8_t  g_duty      = MIST_DUTY_FULL;
static bool     g_scope     = false;
static uint32_t g_lastStatMs  = 0;
static uint32_t g_lastScopeMs = 0;
static bool     g_bootBtnWas  = false;      // BOOT edge detect (active LOW)
static uint32_t g_bootEdgeMs  = 0;

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
  Serial.println("[HELP] a (or press BOOT) = automated self-test");
}

// ===========================================================================
// AUTOMATED SELF-TEST
//
// The Extension is the simplest board — no battery, boost, LED, or button —
// so the checkout is short: clean boot, INA180 zero, and the drive burst
// classified against the measured current bands. Same report format as the
// Battery Kit self-test (aligned rows, verdict, one "SELFTEST," summary line
// for logs), so batch QC across both kits greps identically.
//
// Runs blocking on purpose: a bench checkout is a fixed sequence, and linear
// code here reads exactly like the protocol it implements.
// ===========================================================================

static const char* T_NAMES[] = { "PASS", "FAIL", "skip", "info", "CHECK" };
static uint8_t g_tCount[5];

static void tRow(const char* id, TestResult r, float val, const char* unit,
                 float lo, float hi, const char* note) {
  g_tCount[r]++;
  Serial.printf("[TEST] %-10s %-5s", id, T_NAMES[r]);
  if (!isnan(val)) Serial.printf(" %7.2f %-3s", val, unit ? unit : "");
  else             Serial.print("             ");
  if (!isnan(lo))  Serial.printf(" [%g..%g]", lo, hi);
  if (note && note[0]) Serial.printf("  %s", note);
  Serial.println();
}

static const char* resetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";   // supply sagged — check cable
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_WDT: case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT:
                            return "watchdog";
    default:                return "other";
  }
}

static void runSelfTest(bool startedByBoot) {
  memset(g_tCount, 0, sizeof(g_tCount));
  const bool wasOn = g_on;
  g_on = false;
  ledcWrite(PIN_MIST_PWM, 0);

  Serial.println();
  Serial.println("[TEST] ==== AUTOMATED SELF-TEST — Extension Kit V0.1 ====");
  Serial.printf("[TEST] fw ExtensionKit_BringUp v%s  build %s\n", FW_VERSION, __DATE__);
  if (wasOn) Serial.println("[TEST] (mist was on — switched off for the test)");

  // 1. RESET — a BROWNOUT here means the USB supply collapsed on the last
  //    run: bad cable/port, or the duty was pushed past what it can source.
  tRow("RESET", esp_reset_reason() != ESP_RST_BROWNOUT ? T_PASS : T_FAIL,
       NAN, nullptr, NAN, NAN, resetReasonStr());

  // 2. IDLE — PWM off: the INA180 zero. Above the limit = offset drift or a
  //    sneak path into the sense node.
  delay(50);
  const float idleMa = readMa(100);
  tRow("IDLE_MA", idleMa <= ST_IDLE_MAX_MA ? T_PASS : T_FAIL,
       idleMa, "mA", 0, ST_IDLE_MAX_MA, "");

  // 3. LOAD — 50% duty for ~1 s, classified against the measured bands.
  //    "No load" is a skip, not a fail: the board may be fine with no disc
  //    plugged in, the drive stage just went unproven.
  Serial.println("[TEST] driving disc at 50% duty (~1 s burst)...");
  ledcWrite(PIN_MIST_PWM, MIST_DUTY_FULL);
  delay(300);                                   // let the plume establish
  const float loadMa = readMa(500);
  ledcWrite(PIN_MIST_PWM, 0);
  digitalWrite(PIN_MIST_PWM, LOW);
  if (loadMa < ST_LOAD_MIN_MA)
    tRow("LOAD_MA", T_SKIP, loadMa, "mA", NAN, NAN,
         "no load — disc unplugged? attach disc in water and re-run");
  else if (loadMa > ST_LOAD_MAX_MA)
    tRow("LOAD_MA", T_FAIL, loadMa, "mA", ST_LOAD_MIN_MA, ST_LOAD_MAX_MA,
         "overcurrent at 50% duty");
  else if (loadMa < ST_DRY_MIN_MA)
    tRow("LOAD_MA", T_FAIL, loadMa, "mA", ST_DRY_MIN_MA, ST_LOAD_MAX_MA,
         "below the dry band — disc seating / drive stage");
  else if (loadMa < ST_WATER_MIN_MA)
    tRow("LOAD_MA", T_PASS, loadMa, "mA", ST_DRY_MIN_MA, ST_WATER_MIN_MA,
         "dry-disc band — add water for the full check");
  else
    tRow("LOAD_MA", T_PASS, loadMa, "mA", ST_WATER_MIN_MA, ST_LOAD_MAX_MA,
         "disc-in-water band");

  // 4. BOOT_BTN — proven by the trigger itself when BOOT started the run;
  //    otherwise a 5 s press window (skipping is fine unattended).
  if (startedByBoot)
    tRow("BOOT_BTN", T_PASS, NAN, nullptr, NAN, NAN, "BOOT press started this run");
  else {
    Serial.printf("[TEST] press BOOT within %u s...\n", ST_BTN_WAIT_MS / 1000);
    const uint32_t t0 = millis();
    bool pressed = false;
    while (millis() - t0 < ST_BTN_WAIT_MS) {
      if (digitalRead(PIN_BOOT_BTN) == LOW) { pressed = true; break; }
      delay(5);
    }
    tRow("BOOT_BTN", pressed ? T_PASS : T_SKIP, NAN, nullptr, NAN, NAN,
         pressed ? "press seen" : "no press — skipped");
    while (digitalRead(PIN_BOOT_BTN) == LOW) delay(5);   // swallow the release
  }

  // ---- verdict ----
  const bool pass = g_tCount[T_FAIL] == 0;
  Serial.println("[TEST] ----------------------------------------------");
  Serial.printf("[TEST] VERDICT: %s  (%u pass, %u fail, %u skip, %u info, %u check)\n",
                pass ? "PASS" : "FAIL", g_tCount[T_PASS], g_tCount[T_FAIL],
                g_tCount[T_SKIP], g_tCount[T_INFO], g_tCount[T_CHECK]);
  Serial.printf("SELFTEST,EXTENSION_KIT,%s,%s,%u,%u,%u,%u,%u\n",
                FW_VERSION, pass ? "PASS" : "FAIL", g_tCount[T_PASS],
                g_tCount[T_FAIL], g_tCount[T_SKIP], g_tCount[T_INFO],
                g_tCount[T_CHECK]);
  Serial.println("[TEST] not covered here: dry-vs-wet separation over time (`s`),");
  Serial.println("[TEST]   dimming by eye (`0`..`9`), duty sweep (DutySweep_Test)");
  Serial.println();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  pinMode(PIN_CURRENT_ADC, INPUT);   // core ADC defaults — do NOT call
                                     // analogReadResolution() on C6 (v3.x bug)
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);  // XIAO BOOT: active LOW, self-test trigger
  ledcAttach(PIN_MIST_PWM, MIST_FREQ_HZ, MIST_PWM_RES);
  ledcWrite(PIN_MIST_PWM, 0);

  Serial.println("==============================================");
  Serial.printf(" Extension Kit V0.1 - BringUp v%s\n", FW_VERSION);
  Serial.println("==============================================");
  printHelp();
}

void loop() {
  // ---- BOOT button (active LOW, 50 ms debounce): press = self-test ----
  const bool bootDown = digitalRead(PIN_BOOT_BTN) == LOW;
  if (bootDown != g_bootBtnWas) { g_bootBtnWas = bootDown; g_bootEdgeMs = millis(); }
  else if (bootDown && g_bootEdgeMs && millis() - g_bootEdgeMs > 50) {
    g_bootEdgeMs = 0;                       // consume this press
    runSelfTest(true);
    while (digitalRead(PIN_BOOT_BTN) == LOW) delay(5);   // wait for release
    g_bootBtnWas = false;
  }

  // ---- serial commands ----
  while (Serial.available()) {
    const char ch = Serial.read();
    if (ch == 't')      { g_on = !g_on; applyOutput(); }
    else if (ch == 'a') { runSelfTest(false); }
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
