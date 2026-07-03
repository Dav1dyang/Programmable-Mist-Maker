// DutySweep_Test — does mist output really peak at ~50% PWM duty?
//
// The MistMaker library caps duty at 127/255 (50%) on the theory that a
// single-transistor resonant drive makes the most mist there and only makes
// more HEAT beyond it. This sketch bypasses the library and sweeps the raw
// LEDC duty 0 -> 100% so you can verify that on real hardware.
//
// What to watch (your eyes are the mist sensor, the board logs the rest):
//   * If the 50% theory holds: visible mist rises up to ~duty 120-140, then
//     FLATTENS or DROPS — while input current KEEPS RISING. Current up +
//     mist down = the extra duty is turning into transistor/tank heat.
//   * If mist keeps clearly increasing past 60-70% duty, the cap is wrong
//     for this board and we should raise the library default.
//
// BENCH RESULT (V0.4 board, 2026-07-03): mist kept INCREASING past 50% —
// but current went superlinear (224 mA @50% -> 1.1 A @75%, ~5x power for
// the gain: inductor saturation territory), and at ~75% total USB draw hit
// ~1.5 A, VBUS sagged, and the board BROWNED OUT mid-sweep (the banner
// reprinting = reboot; the TPS2116 drops USB when VBUS < 4.0 V). So 50%
// is the efficiency/stability knee, not a mist maximum. The sweep now
// aborts above SWEEP_ABORT_MA to fail gracefully instead of rebooting.
//
// Setup: Battery Kit V0.4 (or V0.3 — same D0/D2/D3), disc IN WATER (never
// sweep a dry disc), USB serial @ 115200. Best in still air, good light.
//
// Commands:
//   w        run the automatic sweep (0 -> 100% in 16 steps, ~3 s each,
//            current logged per step, summary table at the end)
//   0..9     hold duty at n*10% (e.g. 5 = 50%) for manual A/B comparison
//   + / -    nudge duty by 4 counts
//   s        toggle CSV stream (duty + mA) for the Serial Plotter
//   x        everything off
//
// Safety: steps above ~60% run the FET/boost hotter than the design point —
// the sweep dwells only 3 s per step and shuts off when done. Don't park at
// high duty for minutes. If anything smells hot, hit 'x'.

#include <Arduino.h>
#include <esp_system.h>   // esp_reset_reason(): names a brownout after the fact

#if defined(ARDUINO_XIAO_ESP32C6) || defined(ARDUINO_XIAO_ESP32S3) || \
    defined(ARDUINO_XIAO_ESP32S3_PLUS) || defined(ARDUINO_XIAO_ESP32C3)
  constexpr uint8_t PIN_MIST_PWM    = D0;   // gate driver input
  constexpr uint8_t PIN_CURRENT_ADC = D2;   // INA180A3 out (3.0 V per A)
  constexpr uint8_t PIN_BOOST_EN    = D3;   // TPS61023 EN (HIGH = 5V rail on)
#else
  #error "Select a XIAO ESP32 board in Tools > Board."
#endif

constexpr uint32_t MIST_FREQ_HZ = 108700;
constexpr uint8_t  PWM_RES_BITS = 8;              // duty 0..255
constexpr float    SENSE_V_PER_A = 3.0f;          // INA180A3 (100 V/V) x 30 mOhm

constexpr uint8_t  SWEEP_STEPS    = 16;           // 0..255 in 17 points
constexpr uint16_t SWEEP_DWELL_MS = 3000;         // per step
constexpr uint16_t SWEEP_SETTLE_MS = 1500;        // dwell before sampling starts
// Abort the sweep above this drive current. Bench (2026-07-03): L1 (the 3-leg
// autotransformer) is already hot to touch at ~485 mA / 62% duty, and ~1.1 A
// / 75% duty sagged USB VBUS into a brownout reset. 600 mA keeps the sweep in
// "informative but not self-destructive" territory; raise it deliberately if
// you're on a beefy supply and watching L1's temperature.
constexpr float SWEEP_ABORT_MA = 600.0f;

static uint8_t  g_duty  = 0;
static bool     g_stream = false;
static uint32_t g_lastStreamMs = 0;

// mA from the INA180. analogReadMilliVolts = factory-calibrated, linear on C6.
static float readMa(uint16_t sampleMs) {
  uint32_t sum_mV = 0, n = 0;
  const uint32_t t0 = millis();
  while (millis() - t0 < sampleMs) { sum_mV += analogReadMilliVolts(PIN_CURRENT_ADC); n++; }
  return n ? (float(sum_mV) / n) / SENSE_V_PER_A : 0.0f;   // mV / (V/A) = mA
}

static void setDuty(uint8_t d) {
  g_duty = d;
  digitalWrite(PIN_BOOST_EN, d > 0 ? HIGH : LOW);
  if (d > 0) delayMicroseconds(500);               // rail settle on first turn-on
  ledcWrite(PIN_MIST_PWM, d);
  Serial.printf("[DUTY] %3u/255 = %5.1f%%\n", d, d * 100.0f / 255.0f);
}

static void sweep() {
  Serial.println();
  Serial.println("[SWEEP] 0 -> 100% duty. WATCH THE MIST at each step and note");
  Serial.println("[SWEEP] where it stops getting denser. ~3 s per step.");
  Serial.println("[SWEEP]  step   duty    duty%   current");

  float ma[SWEEP_STEPS + 1];
  uint8_t duties[SWEEP_STEPS + 1];
  uint8_t ran = 0;                                 // steps actually measured

  for (uint8_t i = 0; i <= SWEEP_STEPS; i++) {
    const uint8_t d = (uint8_t)((uint16_t)i * 255 / SWEEP_STEPS);
    duties[i] = d;
    digitalWrite(PIN_BOOST_EN, HIGH);
    ledcWrite(PIN_MIST_PWM, d);
    delay(SWEEP_SETTLE_MS);                        // let drive + water column respond
    ma[i] = readMa(SWEEP_DWELL_MS - SWEEP_SETTLE_MS);
    ran = i + 1;
    Serial.printf("[SWEEP]  %2u/%u   %3u   %5.1f%%   %6.1f mA\n",
                  i, SWEEP_STEPS, d, d * 100.0f / 255.0f, ma[i]);
    if (ma[i] > SWEEP_ABORT_MA) {
      Serial.printf("[SWEEP] %.0f mA > %.0f mA limit — stopping here. Past this\n"
                    "[SWEEP] point L1 saturates (runs hot) and USB VBUS sag can\n"
                    "[SWEEP] brown-out the board. Raise SWEEP_ABORT_MA to explore.\n",
                    ma[i], SWEEP_ABORT_MA);
      break;
    }
    if (Serial.available() && Serial.read() == 'x') { Serial.println("[SWEEP] aborted"); break; }
  }
  setDuty(0);

  // Summary: current alone can't see the mist peak (current keeps rising past
  // it) — so print the table for pairing with what you SAW.
  Serial.println();
  Serial.println("[RESULT] duty%, mA   (paste next to your mist notes)");
  for (uint8_t i = 0; i < ran; i++)
    Serial.printf("%5.1f, %.1f\n", duties[i] * 100.0f / 255.0f, ma[i]);
  Serial.println();
  Serial.println("[RESULT] Reading it: if current rose monotonically but mist");
  Serial.println("[RESULT] peaked near 50%, the library's 127 cap is correct.");
  Serial.println("[RESULT] If mist tracked current all the way up, tell Claude");
  Serial.println("[RESULT] — the cap should be raised.");
}

static void printHelp() {
  Serial.println("[HELP] w=auto sweep  0..9=hold n*10% duty  +/-=nudge  s=stream  x=off");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(PIN_CURRENT_ADC, INPUT);
  pinMode(PIN_BOOST_EN, OUTPUT);
  digitalWrite(PIN_BOOST_EN, LOW);
  ledcAttach(PIN_MIST_PWM, MIST_FREQ_HZ, PWM_RES_BITS);
  ledcWrite(PIN_MIST_PWM, 0);

  Serial.println("==============================================");
  Serial.println(" Duty Sweep - does mist peak at 50% duty?");
  Serial.println("==============================================");
  // If the previous run ended in a mid-sweep reboot, this line names it:
  // "brownout" = the supply sagged under the high-duty load.
  const esp_reset_reason_t rr = esp_reset_reason();
  Serial.printf("[BOOT] reset: %s\n",
                rr == ESP_RST_BROWNOUT ? "BROWNOUT (supply sagged under load!)"
                : rr == ESP_RST_POWERON ? "power-on"
                : rr == ESP_RST_SW      ? "software" : "other");
  Serial.println("[!] Disc must be IN WATER before you start.");
  printHelp();
}

void loop() {
  while (Serial.available()) {
    const char ch = Serial.read();
    if      (ch == 'w') sweep();
    else if (ch == 'x') setDuty(0);
    else if (ch == 's') { g_stream = !g_stream;
                          Serial.printf("[CFG] stream %s\n", g_stream ? "ON" : "OFF"); }
    else if (ch == '+') setDuty(g_duty <= 251 ? g_duty + 4 : 255);
    else if (ch == '-') setDuty(g_duty >= 4 ? g_duty - 4 : 0);
    else if (ch >= '0' && ch <= '9') setDuty((uint8_t)((ch - '0') * 255 / 10));
    else if (ch == 'h') printHelp();
  }

  if (g_stream && millis() - g_lastStreamMs >= 100) {
    g_lastStreamMs = millis();
    Serial.printf("duty:%u\tmA:%.1f\n", g_duty, readMa(50));
  }
}
