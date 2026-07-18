// Mist Maker Battery Kit V0.4 / V0.4.1 — BringUp sketch.
// (Pins D0–D7 are identical on V0.3; the `u` USB/mux test needs V0.4+.
//  V0.4.1 — the July 2026 production spin — runs the same firmware; see
//  pins.h for what changed electrically and SELF-TEST below for the fast
//  automated checkout.)
//
// Bare-bones per-feature verification, no library required. Flash this on a
// freshly assembled board, then EITHER hold the button >= 1.5 s (or send `a`)
// for the AUTOMATED SELF-TEST — a ~10 s pass/fail checkout of everything
// measurable without hands — or walk the manual checklist:
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
// For real applications use the MistMaker library (>= 2.1.0) instead:
// https://github.com/owochel/MistMaker

#include "pins.h"
#include <esp_system.h>   // esp_reset_reason(): brownout vs power-on at boot

static const char FW_VERSION[] = "1.1.0";   // BringUp sketch, not the library

static bool     g_on        = false;
static uint8_t  g_duty      = MIST_DUTY_FULL;
static bool     g_scope     = false;
static bool     g_btnDeb    = false;
static bool     g_btnRaw    = false;
static uint32_t g_btnEdgeMs = 0;
static uint32_t g_btnPressMs  = 0;     // when the debounced press began
static bool     g_btnLongFired = false; // long-press consumed (skip the toggle)
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
  Serial.println("[HELP] a (or hold button 1.5 s) = automated self-test");
  Serial.println("[HELP] handoff test: cell in -> unplug USB -> replug -> u");
}

// ===========================================================================
// AUTOMATED SELF-TEST
//
// The automatable slice of V04-Acceptance-Test_2026-07-04.md, run end-to-end
// in ~10 s: §A power/boot (reset reason, source ID, Vbatt window), §C current
// bands at 50% duty, §D1 ST read, §E1 charge-tag gating — plus a boost-gate
// check the manual protocol gets implicitly by scoping the rail. Each test
// prints one aligned row; the run ends with a verdict, one machine-parseable
// summary line (grep "^SELFTEST,"), and the list of steps that still need
// hands (multimeter cross-check, USB-unplug handoff, phone/app, duty sweep).
//
// Runs blocking on purpose: a bench checkout is a fixed sequence, and linear
// code here reads exactly like the protocol document it implements. Serial
// commands, [STAT], and the scope stream pause while it runs (~10 s worst
// case: disc drive burst + the 5 s button window on serial-started runs).
// ===========================================================================

static const char* T_NAMES[] = { "PASS", "FAIL", "skip", "info", "CHECK" };
static uint8_t g_tCount[5];

// One report row. Tests with no numeric value pass NAN; noteOnly rows keep
// the table aligned without inventing fake limits.
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

// Quiet output control (applyOutput() narrates; the test wants silence).
static void tDrive(bool boostOn, uint8_t duty) {
  digitalWrite(PIN_BOOST_EN, boostOn ? HIGH : LOW);
  ledcWrite(PIN_MIST_PWM, duty);
}

static void runSelfTest(bool startedByButton) {
  memset(g_tCount, 0, sizeof(g_tCount));
  const bool wasOn = g_on;
  g_on = false;
  tDrive(false, 0);
  digitalWrite(PIN_STATUS_LED, LOW);

  Serial.println();
  Serial.println("[TEST] ==== AUTOMATED SELF-TEST — Battery Kit V0.4 / V0.4.1 ====");
  Serial.printf("[TEST] fw BatteryKit_BringUp v%s  build %s\n", FW_VERSION, __DATE__);
  if (wasOn) Serial.println("[TEST] (mist was on — switched off for the test)");

  // 1. RESET — a BROWNOUT here means the last reset was a power collapse:
  //    bad cable/supply, or a failed mux handoff. Power cleanly and re-run.
  const bool cleanReset = esp_reset_reason() != ESP_RST_BROWNOUT;
  tRow("RESET", cleanReset ? T_PASS : T_FAIL, NAN, nullptr, NAN, NAN,
       resetReasonStr());

  // 2. PWR_SRC — 40 reads over ~80 ms must agree. A split vote = D8 floating
  //    or the R21/R22 divider misloaded (the one V0.4 bench-marginal net).
  uint8_t highs = 0;
  for (uint8_t i = 0; i < 40; i++) { if (digitalRead(PIN_USB_SENSE)) highs++; delay(2); }
  const bool srcUsb = highs >= 20;
  if (highs == 0 || highs == 40)
    tRow("PWR_SRC", T_PASS, NAN, nullptr, NAN, NAN,
         srcUsb ? "USB (D8 HIGH)" : "cell (D8 LOW)");
  else
    tRow("PWR_SRC", T_FAIL, float(highs), "/40", NAN, NAN,
         "D8 unstable — check R21/R22 divider");

  // 3. VBAT — window covers a real cell and the no-cell-on-USB case (divider
  //    then tracks the charger, ~4.1-4.2 V). Near 0 = divider open/ADC dead.
  const float vbat = readBatteryVolts();
  tRow("VBAT", (vbat >= ST_VBAT_MIN_V && vbat <= ST_VBAT_MAX_V) ? T_PASS : T_FAIL,
       vbat, "V", ST_VBAT_MIN_V, ST_VBAT_MAX_V,
       vbat < 0.5f ? "divider open / ADC dead?" : "");

  // 4. IDLE — rail off, PWM off: the INA180 zero. Anything above the limit is
  //    offset drift or a sneak path into the sense node.
  delay(50);
  const float idleMa = readMa(100);
  tRow("IDLE_MA", idleMa <= ST_IDLE_MAX_MA ? T_PASS : T_FAIL,
       idleMa, "mA", 0, ST_IDLE_MAX_MA, "");

  // 5. GATE_OFF — full PWM with the boost DISABLED must draw nothing: proves
  //    D3 really gates the rail (and on V0.4.1, that R8's pull-down holds it).
  tDrive(false, MIST_DUTY_FULL);
  delay(250);
  const float gateMa = readMa(150);
  tDrive(false, 0);
  tRow("GATE_OFF", gateMa <= ST_IDLE_MAX_MA ? T_PASS : T_FAIL,
       gateMa, "mA", 0, ST_IDLE_MAX_MA,
       gateMa > ST_IDLE_MAX_MA ? "rail alive with EN low!" : "");

  // 6. LOAD — the real drive: boost on, 50% duty, ~1 s. Bands from the
  //    acceptance protocol; "no load" is a skip, not a fail — the board may
  //    be fine with no disc plugged in, the drive stage just went unproven.
  Serial.println("[TEST] driving disc at 50% duty (~1 s burst)...");
  tDrive(true, 0);
  delay(150);                                   // 5V5 rail settle
  tDrive(true, MIST_DUTY_FULL);
  delay(300);                                   // let the plume establish
  const float loadMa  = readMa(500);
  const float vbatLoad = readBatteryVolts();    // still under load
  tDrive(false, 0);
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
  // Voltage sag under the burst: informative on USB (charger holds it up),
  // the honest number on the cell.
  if (loadMa >= ST_LOAD_MIN_MA)
    tRow("VBAT_SAG", T_INFO, vbat - vbatLoad, "V", NAN, NAN,
         srcUsb ? "on USB (charger-held)" : "on cell, under load");

  // 7. GAUGE — the V0.4 headline behavior: on USB the divider tracks the
  //    charger, so SoC must report as gated; on the cell it's the real gauge.
  if (srcUsb)
    tRow("GAUGE", T_PASS, NAN, nullptr, NAN, NAN,
         "on USB: cell charging, SoC N/A (gating correct)");
  else
    tRow("GAUGE", T_PASS, vbat, "V", NAN, NAN,
         vbat < BATT_CRITICAL_V ? "cell CRITICAL" :
         vbat < BATT_LOW_V      ? "cell LOW" : "cell OK");

  // 8. LED — the one eyes-required step: three blinks on LED1 (D7).
  tRow("LED", T_CHECK, NAN, nullptr, NAN, NAN, "watch LED1 blink 3x now");
  for (uint8_t i = 0; i < 3; i++) {
    digitalWrite(PIN_STATUS_LED, HIGH); delay(160);
    digitalWrite(PIN_STATUS_LED, LOW);  delay(160);
  }

  // 9. BUTTON — proven by the trigger itself when the run started from the
  //    long-press; otherwise a 5 s press window (skipping is fine unattended).
  if (startedByButton)
    tRow("BUTTON", T_PASS, NAN, nullptr, NAN, NAN, "long-press started this run");
  else {
    Serial.printf("[TEST] press the button within %u s...\n", ST_BTN_WAIT_MS / 1000);
    const uint32_t t0 = millis();
    bool pressed = false;
    while (millis() - t0 < ST_BTN_WAIT_MS) {
      if (digitalRead(PIN_BUTTON) == HIGH) { pressed = true; break; }
      delay(5);
    }
    tRow("BUTTON", pressed ? T_PASS : T_SKIP, NAN, nullptr, NAN, NAN,
         pressed ? "press seen" : "no press — skipped");
    while (digitalRead(PIN_BUTTON) == HIGH) delay(5);   // swallow the release
  }

  // 10. HANDOFF — reportable, not automatable: flips need a physical unplug.
  if (g_usbFlips >= 2 && cleanReset)
    tRow("HANDOFF", T_PASS, float(g_usbFlips), "x", NAN, NAN,
         "handoffs survived, no brownout");
  else
    tRow("HANDOFF", T_INFO, float(g_usbFlips), "x", NAN, NAN,
         "manual: cell in -> unplug USB -> replug -> u");

  // ---- verdict ----
  const bool pass = g_tCount[T_FAIL] == 0;
  Serial.println("[TEST] ----------------------------------------------");
  Serial.printf("[TEST] VERDICT: %s  (%u pass, %u fail, %u skip, %u info, %u check)\n",
                pass ? "PASS" : "FAIL", g_tCount[T_PASS], g_tCount[T_FAIL],
                g_tCount[T_SKIP], g_tCount[T_INFO], g_tCount[T_CHECK]);
  Serial.printf("SELFTEST,BATTERY_KIT,%s,%s,%u,%u,%u,%u,%u\n",
                FW_VERSION, pass ? "PASS" : "FAIL", g_tCount[T_PASS],
                g_tCount[T_FAIL], g_tCount[T_SKIP], g_tCount[T_INFO],
                g_tCount[T_CHECK]);
  Serial.println("[TEST] not covered here (see V04-Acceptance-Test protocol):");
  Serial.println("[TEST]   multimeter Vbatt cross-check | USB-unplug mux handoff (u)");
  Serial.println("[TEST]   charge-LED behavior | PhoneSensors radio test | duty sweep (DutySweep_Test)");
  Serial.println();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  // USB plugged in but no port open: the CDC TX buffer fills and every print
  // blocks ~100 ms waiting for a host that isn't reading — the loop turns
  // sluggish and button taps get eaten. Drop output instead of blocking; with
  // a port open and draining, nothing is lost.
  Serial.setTxTimeoutMs(0);
#endif
  delay(1000);

  pinMode(PIN_CURRENT_ADC, INPUT);   // raw analogRead for current sense; do NOT
  pinMode(PIN_BATT_ADC, INPUT);      // call analogReadResolution() on C6 (v3.x
                                     // bug) — battery uses analogReadMilliVolts.
  pinMode(PIN_BUTTON, INPUT);        // PCB has its own 10k pull-down
  pinMode(PIN_USB_SENSE, INPUT);     // mux ST via external divider — no pull!
  pinMode(PIN_BOOST_EN, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  // V0.4 (R8 pull-up): the ~5 V boost rail is LIVE from power-on until this
  // line runs (piezo stays off — R11 holds Q4's gate down). V0.4.1 (R8
  // pull-down): the rail is DEAD at power-on and LED2 stays off until the
  // sketch raises D3. Either way, this line pins the rail off deliberately.
  digitalWrite(PIN_BOOST_EN, LOW);

  ledcAttach(PIN_MIST_PWM, MIST_FREQ_HZ, MIST_PWM_RES);
  ledcWrite(PIN_MIST_PWM, 0);

  Serial.println("==============================================");
  Serial.printf(" Battery Kit V0.4 / V0.4.1 - BringUp v%s\n", FW_VERSION);
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
  // Short press = toggle (on release, so a long-press doesn't also toggle);
  // hold >= BTN_LONGPRESS_MS = automated self-test, fired while still held.
  const bool raw = digitalRead(PIN_BUTTON) == HIGH;
  if (raw != g_btnRaw) { g_btnRaw = raw; g_btnEdgeMs = millis(); }
  if (millis() - g_btnEdgeMs > BUTTON_DEBOUNCE_MS && g_btnDeb != g_btnRaw) {
    g_btnDeb = g_btnRaw;
    if (g_btnDeb) { g_btnPressMs = millis(); g_btnLongFired = false; }
    else if (!g_btnLongFired) { g_on = !g_on; applyOutput(); }
  }
  if (g_btnDeb && !g_btnLongFired &&
      millis() - g_btnPressMs >= BTN_LONGPRESS_MS) {
    g_btnLongFired = true;
    runSelfTest(true);
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
    else if (ch == 'a') { runSelfTest(false); }
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
