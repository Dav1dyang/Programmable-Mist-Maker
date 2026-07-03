// DutySweep_Test — how much mist can the V0.4 board actually make?
//
// v1 asked "does mist peak at 50% duty?" — bench answer (2026-07-03): NO,
// mist keeps rising past 50% on this flyback-style drive, but current goes
// superlinear (224 mA @50% -> 1.1 A @75%), L1 runs hot by ~62%, and ~1.5 A
// total USB draw at 75% sags VBUS into a brownout (TPS2116 drops USB below
// 4.0 V). So the USB port, not the circuit, set the ceiling.
//
// v2 adds the tools to push past that ceiling safely:
//   * runtime current limit (`L` cycles 600 -> 900 -> 1200 mA). 1200 is the
//     hard max: D1 (PMEG10010, series Schottky in the drive feed) is rated
//     1 A continuous — brief steps above it are survivable, parking is not.
//   * BATTERY SWEEP (`B`): the cell can source what USB can't. Arm it over
//     serial, unplug USB, and the board runs the sweep unattended from the
//     cell (duty 50 -> 90%), logging every step to flash (NVS) so even a
//     brownout can't lose the data. Replug USB — the table replays.
//     Guards: current > 1.2 A, loaded Vbatt < 3.5 V (the 3V3 LDO drops out
//     ~3.65 V — abort before the XIAO browns out), USB replug mid-sweep.
//   * duty hard-cap 230/255 (~90%) everywhere: at 108.7 kHz the period is
//     9.2 us and the resonant ring-back needs ~4.6 us of off-time — above
//     ~90% there is no ring, no mist, and L1 becomes a DC heater. 100% duty
//     is a short circuit with extra steps, not "max mist".
//
// Thermal protocol: L1 is hot to touch from ~485 mA. High-duty steps dwell
// only ~1.5 s with an off-cooldown between; let the board rest a minute or
// two between full runs, and touch-check L1. Disc IN WATER, always.
//
// Commands (serial 115200):
//   w        USB sweep 0 -> 90% (~3 s/step, aborts at the current limit)
//   B        arm the battery sweep, then unplug USB within 30 s
//   L        cycle the current limit: 600 / 900 / 1200 mA
//   0..9     hold duty at n*10% (capped at 90%)   + / -  nudge by 4
//   s        CSV stream (duty, mA, Vbatt, USB) for the Serial Plotter
//   x        everything off / disarm    h  help

#include <Arduino.h>
#include <esp_system.h>    // esp_reset_reason(): names a brownout after the fact
#include <Preferences.h>   // NVS: battery-sweep black box, survives any reset

#if defined(ARDUINO_XIAO_ESP32C6) || defined(ARDUINO_XIAO_ESP32S3) || \
    defined(ARDUINO_XIAO_ESP32S3_PLUS) || defined(ARDUINO_XIAO_ESP32C3)
  constexpr uint8_t PIN_MIST_PWM    = D0;   // gate driver input
  constexpr uint8_t PIN_BATT_ADC    = D1;   // battery via 10k/10k divider
  constexpr uint8_t PIN_CURRENT_ADC = D2;   // INA180A3 out (3.0 V per A)
  constexpr uint8_t PIN_BOOST_EN    = D3;   // TPS61023 EN (HIGH = 5V rail on)
  constexpr uint8_t PIN_USB_SENSE   = D8;   // TPS2116 ST: HIGH = USB, LOW = cell
#else
  #error "Select a XIAO ESP32 board in Tools > Board."
#endif

constexpr uint32_t MIST_FREQ_HZ  = 108700;
constexpr uint8_t  PWM_RES_BITS  = 8;
constexpr float    SENSE_V_PER_A = 3.0f;    // INA180A3 (100 V/V) x 30 mOhm
constexpr float    BATT_DIVIDER  = 2.0f;    // 10k/10k

// ~90% of 255. Above this the off-window can't fit the resonant ring-back:
// no mist, just DC heating in L1. Applies to sweeps AND manual duty.
constexpr uint8_t DUTY_HARD_MAX = 230;

// USB sweep (attended, ~3 s/step)
constexpr uint8_t  SWEEP_STEPS     = 16;
constexpr uint16_t SWEEP_DWELL_MS  = 3000;
constexpr uint16_t SWEEP_SETTLE_MS = 1500;

// Battery sweep (unattended): duty 127 -> 230 in 13-count steps, short dwell
// + off-cooldown to keep L1 survivable at the currents involved.
constexpr uint8_t  BSWEEP_START    = 127;
constexpr uint8_t  BSWEEP_STEP     = 13;
constexpr uint16_t BSWEEP_SETTLE_MS   = 500;
constexpr uint16_t BSWEEP_SAMPLE_MS   = 1000;
constexpr uint16_t BSWEEP_COOLDOWN_MS = 1000;
constexpr uint32_t BSWEEP_ARM_TIMEOUT_MS = 30000;
constexpr float    BSWEEP_MIN_VBATT = 3.50f;  // loaded; LDO dropout ~3.65 V
constexpr uint8_t  BSWEEP_MAX_STEPS = 10;

constexpr float LIMIT_STEPS_MA[] = { 600.0f, 900.0f, 1200.0f };  // 1200 = D1's edge

static uint8_t  g_duty = 0;
static bool     g_stream = false;
static uint32_t g_lastStreamMs = 0;
static uint8_t  g_limitIdx = 0;
static bool     g_armed = false;
static uint32_t g_armedAtMs = 0;
Preferences prefs;

struct BsweepLog {                 // black box, one NVS blob
  uint8_t  count;                  // steps recorded
  uint8_t  done;                   // 1 = sweep finished cleanly
  uint8_t  unread;                 // 1 = not yet replayed over serial
  uint8_t  abortReason;            // 0 ok, 1 overcurrent, 2 vbatt, 3 usb-replug
  uint8_t  duty[BSWEEP_MAX_STEPS];
  float    ma[BSWEEP_MAX_STEPS];
  float    vb[BSWEEP_MAX_STEPS];
};
static BsweepLog g_log;

static float limitMa() { return LIMIT_STEPS_MA[g_limitIdx]; }
static bool  onUsb()   { return digitalRead(PIN_USB_SENSE) == HIGH; }

static float readMa(uint16_t sampleMs) {
  uint32_t sum_mV = 0, n = 0;
  const uint32_t t0 = millis();
  while (millis() - t0 < sampleMs) { sum_mV += analogReadMilliVolts(PIN_CURRENT_ADC); n++; }
  return n ? (float(sum_mV) / n) / SENSE_V_PER_A : 0.0f;   // mV / (V/A) = mA
}

static float readVbatt() {
  uint32_t sum_mV = 0;
  for (uint8_t i = 0; i < 16; i++) sum_mV += analogReadMilliVolts(PIN_BATT_ADC);
  return (float(sum_mV) / 16.0f) / 1000.0f * BATT_DIVIDER;
}

static void setDuty(uint8_t d) {
  if (d > DUTY_HARD_MAX) {
    Serial.printf("[DUTY] %u capped to %u (~90%%): past that the ring-back has no\n"
                  "[DUTY] time to swing — no mist, L1 just heats as a DC load.\n",
                  d, DUTY_HARD_MAX);
    d = DUTY_HARD_MAX;
  }
  g_duty = d;
  digitalWrite(PIN_BOOST_EN, d > 0 ? HIGH : LOW);
  if (d > 0) delayMicroseconds(500);
  ledcWrite(PIN_MIST_PWM, d);
  Serial.printf("[DUTY] %3u/255 = %5.1f%%\n", d, d * 100.0f / 255.0f);
}

static void saveLog() { prefs.putBytes("blog", &g_log, sizeof(g_log)); }

static void printBsweepTable(const char* header) {
  Serial.println();
  Serial.println(header);
  Serial.println("[BATT]  duty%   drive-mA   Vbatt");
  for (uint8_t i = 0; i < g_log.count; i++)
    Serial.printf("[BATT]  %5.1f   %7.1f   %5.2f V\n",
                  g_log.duty[i] * 100.0f / 255.0f, g_log.ma[i], g_log.vb[i]);
  const char* why = g_log.abortReason == 1 ? "current limit (D1 rating)"
                  : g_log.abortReason == 2 ? "cell sagged (LDO dropout guard)"
                  : g_log.abortReason == 3 ? "USB replugged mid-sweep"
                  : g_log.done             ? "completed full range"
                                           : "DID NOT FINISH (reset mid-sweep?)";
  Serial.printf("[BATT] end: %s\n", why);
}

// ---------------- USB (attended) sweep ----------------
static void sweep() {
  Serial.println();
  Serial.printf("[SWEEP] 0 -> 90%% duty, limit %.0f mA ('L' raises it). WATCH THE MIST.\n", limitMa());
  Serial.println("[SWEEP]  step   duty    duty%   current");

  float ma[SWEEP_STEPS + 1];
  uint8_t duties[SWEEP_STEPS + 1];
  uint8_t ran = 0;

  for (uint8_t i = 0; i <= SWEEP_STEPS; i++) {
    uint8_t d = (uint8_t)((uint16_t)i * 255 / SWEEP_STEPS);
    if (d > DUTY_HARD_MAX) d = DUTY_HARD_MAX;
    duties[i] = d;
    digitalWrite(PIN_BOOST_EN, HIGH);
    ledcWrite(PIN_MIST_PWM, d);
    delay(SWEEP_SETTLE_MS);
    ma[i] = readMa(SWEEP_DWELL_MS - SWEEP_SETTLE_MS);
    ran = i + 1;
    Serial.printf("[SWEEP]  %2u/%u   %3u   %5.1f%%   %6.1f mA\n",
                  i, SWEEP_STEPS, d, d * 100.0f / 255.0f, ma[i]);
    if (ma[i] > limitMa()) {
      Serial.printf("[SWEEP] %.0f mA > %.0f mA limit — stopping. 'L' raises the limit\n"
                    "[SWEEP] (max 1200 mA = D1's rating); 'B' sweeps from the battery\n"
                    "[SWEEP] instead, which dodges the USB brownout entirely.\n",
                    ma[i], limitMa());
      break;
    }
    if (Serial.available() && Serial.read() == 'x') { Serial.println("[SWEEP] aborted"); break; }
    if (duties[i] == DUTY_HARD_MAX) break;
  }
  setDuty(0);

  Serial.println();
  Serial.println("[RESULT] duty%, mA   (paste next to your mist notes)");
  for (uint8_t i = 0; i < ran; i++)
    Serial.printf("%5.1f, %.1f\n", duties[i] * 100.0f / 255.0f, ma[i]);
}

// ---------------- battery (unattended) sweep ----------------
// Runs with serial dead. Every step lands in NVS before the next begins, so
// the table survives a brownout, a cell collapse, or a mid-sweep reset.
static void batterySweep() {
  memset(&g_log, 0, sizeof(g_log));
  g_log.unread = 1;
  saveLog();

  for (uint8_t i = 0; i < BSWEEP_MAX_STEPS; i++) {
    uint16_t d16 = BSWEEP_START + (uint16_t)i * BSWEEP_STEP;
    const uint8_t d = d16 > DUTY_HARD_MAX ? DUTY_HARD_MAX : (uint8_t)d16;

    digitalWrite(PIN_BOOST_EN, HIGH);
    ledcWrite(PIN_MIST_PWM, d);
    delay(BSWEEP_SETTLE_MS);
    const float ma = readMa(BSWEEP_SAMPLE_MS);
    const float vb = readVbatt();               // still under load

    g_log.duty[i] = d; g_log.ma[i] = ma; g_log.vb[i] = vb;
    g_log.count = i + 1;

    if      (ma > limitMa())          g_log.abortReason = 1;
    else if (vb < BSWEEP_MIN_VBATT)   g_log.abortReason = 2;
    else if (onUsb())                 g_log.abortReason = 3;
    saveLog();                                   // step is safe in flash NOW

    ledcWrite(PIN_MIST_PWM, 0);                  // off-cooldown for L1
    delay(BSWEEP_COOLDOWN_MS);

    if (g_log.abortReason || d == DUTY_HARD_MAX) break;
  }
  g_log.done = 1;
  saveLog();
  ledcWrite(PIN_MIST_PWM, 0);
  digitalWrite(PIN_BOOST_EN, LOW);
}

static void printHelp() {
  Serial.printf("[HELP] w=USB sweep  B=arm battery sweep  L=limit (now %.0f mA)\n", limitMa());
  Serial.println("[HELP] 0..9=duty n*10%  +/-=nudge  s=stream  x=off  h=help");
  Serial.println("[HELP] battery sweep: press B, unplug USB, watch mist, replug, read table");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(PIN_CURRENT_ADC, INPUT);
  pinMode(PIN_BATT_ADC, INPUT);
  pinMode(PIN_USB_SENSE, INPUT);     // external divider — no internal pull!
  pinMode(PIN_BOOST_EN, OUTPUT);
  digitalWrite(PIN_BOOST_EN, LOW);
  ledcAttach(PIN_MIST_PWM, MIST_FREQ_HZ, PWM_RES_BITS);
  ledcWrite(PIN_MIST_PWM, 0);

  prefs.begin("dsweep");

  Serial.println("==============================================");
  Serial.println(" Duty Sweep v2 - find the V0.4 board's max");
  Serial.println("==============================================");
  const esp_reset_reason_t rr = esp_reset_reason();
  Serial.printf("[BOOT] reset: %s\n",
                rr == ESP_RST_BROWNOUT ? "BROWNOUT (supply sagged under load!)"
                : rr == ESP_RST_POWERON ? "power-on"
                : rr == ESP_RST_SW      ? "software" : "other");

  // Black box from a previous battery sweep? Replay it.
  if (prefs.getBytes("blog", &g_log, sizeof(g_log)) == sizeof(g_log) && g_log.unread && g_log.count) {
    printBsweepTable("[BATT] ==== battery sweep results (from flash) ====");
    if (!g_log.done && rr == ESP_RST_BROWNOUT)
      Serial.println("[BATT] the sweep itself browned the board out — that step is the wall.");
    g_log.unread = 0;
    saveLog();
  }

  Serial.println("[!] Disc must be IN WATER before you start.");
  printHelp();
}

void loop() {
  // Armed battery sweep: wait for the unplug, then run unattended.
  if (g_armed) {
    if (!onUsb()) {
      delay(2000);                               // let the unplug settle
      batterySweep();
      g_armed = false;                           // done; table prints on replug/reboot
    } else if (millis() - g_armedAtMs > BSWEEP_ARM_TIMEOUT_MS) {
      g_armed = false;
      Serial.println("[BATT] arm timed out (30 s) — press B and unplug sooner.");
    }
  }

  // Replay results the moment serial + USB are back and a table is unread.
  static uint32_t lastReplayCheck = 0;
  if (onUsb() && millis() - lastReplayCheck > 1000) {
    lastReplayCheck = millis();
    if (g_log.unread && g_log.count && g_log.done) {
      printBsweepTable("[BATT] ==== battery sweep results ====");
      g_log.unread = 0;
      saveLog();
    }
  }

  while (Serial.available()) {
    const char ch = Serial.read();
    if      (ch == 'w') sweep();
    else if (ch == 'B') {
      if (!onUsb()) { Serial.println("[BATT] already on battery — plug USB in, then arm."); }
      else {
        g_armed = true; g_armedAtMs = millis();
        Serial.printf("[BATT] ARMED (limit %.0f mA, floor %.2f V, duty 50->90%%).\n",
                      limitMa(), BSWEEP_MIN_VBATT);
        Serial.println("[BATT] Unplug USB within 30 s. Watch the mist per step (~2.5 s each).");
        Serial.println("[BATT] Replug when the mist stops — the table replays here.");
      }
    }
    else if (ch == 'L') {
      g_limitIdx = (g_limitIdx + 1) % (sizeof(LIMIT_STEPS_MA) / sizeof(LIMIT_STEPS_MA[0]));
      Serial.printf("[CFG] current limit -> %.0f mA%s\n", limitMa(),
                    g_limitIdx == 2 ? "  (D1's 1 A rating — short steps only, watch L1!)" : "");
    }
    else if (ch == 'x') { g_armed = false; setDuty(0); }
    else if (ch == 's') { g_stream = !g_stream;
                          Serial.printf("[CFG] stream %s\n", g_stream ? "ON" : "OFF"); }
    else if (ch == '+') setDuty(g_duty + 4 > DUTY_HARD_MAX ? DUTY_HARD_MAX : g_duty + 4);
    else if (ch == '-') setDuty(g_duty >= 4 ? g_duty - 4 : 0);
    else if (ch >= '0' && ch <= '9') setDuty((uint8_t)((ch - '0') * 255 / 10));
    else if (ch == 'h') printHelp();
  }

  if (g_stream && millis() - g_lastStreamMs >= 100) {
    g_lastStreamMs = millis();
    Serial.printf("duty:%u\tmA:%.1f\tVbatt:%.2f\tUSB:%d\n",
                  g_duty, readMa(50), readVbatt(), onUsb() ? 1 : 0);
  }
}
