// DutySweep_Test — how much mist can the board actually make?
//
// Works on the Battery Kit V0.4 AND the Seeed Extension Kit V0.1 (pick your
// board below). Bench story so far (V0.4, 2026-07-03): mist keeps rising past
// 50% duty on this flyback-style drive, but current goes superlinear
// (224 mA @50% -> 1.1 A @75%), L1 runs hot by ~62%, a laptop USB port CUTS
// POWER (~1.5 A, reset reason "power-on"), and a half-charged cell hits the
// LDO-dropout floor at 60%. The circuit's own limits (D1's 1 A rating, the
// 90% duty ceiling) haven't been reached yet — that's what this build is for.
//
// Why the Extension Kit is a good max-test vehicle: same drive stage as V0.4
// (L1 / FET / D1 / 30 mOhm shunt), but no charger stealing ~200 mA and no
// TPS2116 whose 4.0 V threshold drops USB mid-sag — plus you can feed it from
// a beefy 5 V wall charger. Note its INA180 runs from 3V3, so current reads
// SATURATE ~1.05 A ("sat!" flag in the tables).
//
// Three ways to run a sweep:
//   w   attended, over serial (0 -> 90%, ~3 s/step, current-limit abort)
//   B   battery sweep (V0.4 only): arm, unplug USB, board sweeps 50 -> 90%
//       from the cell, logs to flash, replays the table when USB returns
//   U   unattended sweep ON NEXT BOOT (any board, any supply): arm, move the
//       board to a wall charger, it boots + waits 8 s + sweeps 50 -> 90%,
//       logging to flash; plug back into the laptop and the table replays.
//
// Safety rails (all sweeps): current limit ('L': 600/900/1200 mA — 1200 is
// D1's PMEG10010 1 A edge, short steps only), loaded Vbatt < 3.5 V abort on
// the cell (3V3 LDO drops out ~3.65 V), duty hard-cap 230/255 (~90%): at
// 108.7 kHz the period is 9.2 us and the ring-back needs ~4.6 us of off-time
// — above ~90% there is no ring, no mist, and L1 is just a DC heater.
// Thermal protocol: short high-duty dwells + off-cooldowns are built in, but
// rest the board a minute between runs and touch-check L1. Disc IN WATER.
//
// Commands: w/B/U as above, L=limit, 0..9=hold duty (capped 90%), +/-=nudge,
//           s=CSV stream, x=off/disarm, h=help.

#include <Arduino.h>
#include <esp_system.h>    // esp_reset_reason(): names a brownout / power cut
#include <Preferences.h>   // NVS black box: sweep logs survive any reset

// ---------------- board select (uncomment exactly ONE) ----------------
#define BOARD_BATTERY_KIT_V04
// #define BOARD_EXTENSION_KIT_V01

#if !defined(ARDUINO_XIAO_ESP32C6) && !defined(ARDUINO_XIAO_ESP32S3) && \
    !defined(ARDUINO_XIAO_ESP32S3_PLUS) && !defined(ARDUINO_XIAO_ESP32C3)
  #error "Select a XIAO ESP32 board in Tools > Board."
#endif

#if defined(BOARD_BATTERY_KIT_V04) && defined(BOARD_EXTENSION_KIT_V01)
  #error "Define exactly ONE board above (comment the other out)."
#endif

#if defined(BOARD_BATTERY_KIT_V04)
  constexpr uint8_t PIN_MIST_PWM    = D0;
  constexpr uint8_t PIN_BATT_ADC    = D1;    // cell via 10k/10k divider
  constexpr uint8_t PIN_CURRENT_ADC = D2;    // INA180A3 (5 V rail -> ~1.55 A range)
  constexpr int8_t  PIN_BOOST_EN    = D3;    // TPS61023 EN
  constexpr int8_t  PIN_USB_SENSE   = D8;    // TPS2116 ST: HIGH = USB
  constexpr float   SENSE_SAT_MA    = 1050.0f;
#elif defined(BOARD_EXTENSION_KIT_V01)
  constexpr uint8_t PIN_MIST_PWM    = D0;
  constexpr int8_t  PIN_BATT_ADC    = -1;    // no cell
  constexpr uint8_t PIN_CURRENT_ADC = D2;    // INA180A3 (3V3 rail -> saturates ~1.05 A)
  constexpr int8_t  PIN_BOOST_EN    = -1;    // piezo rail = VBUS, always on
  constexpr int8_t  PIN_USB_SENSE   = -1;    // no mux
  constexpr float   SENSE_SAT_MA    = 1050.0f;
#else
  #error "Uncomment one BOARD_* define above."
#endif

constexpr uint32_t MIST_FREQ_HZ  = 108700;
constexpr uint8_t  PWM_RES_BITS  = 8;
constexpr float    SENSE_V_PER_A = 3.0f;    // INA180A3 (100 V/V) x 30 mOhm
constexpr float    BATT_DIVIDER  = 2.0f;

// ~90% of 255 — above this the off-window can't fit the resonant ring-back.
constexpr uint8_t DUTY_HARD_MAX = 230;

// Attended USB sweep
constexpr uint8_t  SWEEP_STEPS     = 16;
constexpr uint16_t SWEEP_DWELL_MS  = 3000;
constexpr uint16_t SWEEP_SETTLE_MS = 1500;

// Unattended sweeps (battery 'B' / on-boot 'U'): 50 -> 90% duty, short dwell
// + off-cooldown to keep L1 survivable at the currents involved.
constexpr uint8_t  USWEEP_START    = 127;
constexpr uint8_t  USWEEP_STEP     = 13;
constexpr uint16_t USWEEP_SETTLE_MS   = 500;
constexpr uint16_t USWEEP_SAMPLE_MS   = 1000;
constexpr uint16_t USWEEP_COOLDOWN_MS = 1000;
constexpr uint32_t BSWEEP_ARM_TIMEOUT_MS = 30000;
constexpr uint32_t USWEEP_BOOT_DELAY_MS  = 8000;
constexpr float    USWEEP_MIN_VBATT = 3.50f;  // loaded; LDO dropout ~3.65 V
constexpr uint8_t  USWEEP_MAX_STEPS = 10;

constexpr float LIMIT_STEPS_MA[] = { 600.0f, 900.0f, 1200.0f };  // 1200 = D1's edge

static uint8_t  g_duty = 0;
static bool     g_stream = false;
static uint32_t g_lastStreamMs = 0;
static uint8_t  g_limitIdx = 0;
static bool     g_armedBatt = false;
static uint32_t g_armedAtMs = 0;
// 'U' with a cell attached: the board never reboots on unplug (the mux hands
// off to the cell), so "next boot" never comes. Instead we watch for USB to
// LEAVE and COME BACK — that's the move to the wall brick.
static bool     g_armUPending = false;
static bool     g_sawUsbDrop  = false;
Preferences prefs;

constexpr uint32_t SWEEP_LOG_MAGIC = 0x4D535732;  // 'MSW2' — bump on layout change

struct SweepLog {                  // black box, one NVS blob
  uint32_t magic;                  // SWEEP_LOG_MAGIC, guards stale/foreign blobs
  uint8_t  count;
  uint8_t  done;                   // 1 = terminal state saved (completed OR controlled abort)
  uint8_t  unread;                 // 1 = not yet replayed over serial
  uint8_t  abortReason;            // 0 ok, 1 overcurrent, 2 vbatt, 3 usb-replug, 4 sensor-saturated
  uint8_t  duty[USWEEP_MAX_STEPS];
  float    ma[USWEEP_MAX_STEPS];
  float    vb[USWEEP_MAX_STEPS];
};
static SweepLog g_log;

// Load the stored blob only if it is exactly our layout and sane — a blob from
// an older sketch version must not partially populate g_log (count drives
// array reads in printLogTable).
static bool loadLog() {
  SweepLog tmp;
  if (prefs.getBytes("blog", &tmp, sizeof(tmp)) == sizeof(tmp) &&
      tmp.magic == SWEEP_LOG_MAGIC && tmp.count <= USWEEP_MAX_STEPS) {
    g_log = tmp;
    return true;
  }
  memset(&g_log, 0, sizeof(g_log));
  g_log.magic = SWEEP_LOG_MAGIC;
  return false;
}

static float limitMa() { return LIMIT_STEPS_MA[g_limitIdx]; }
static bool  onUsb() {                       // no mux ST pin -> assume USB
  return PIN_USB_SENSE < 0 || digitalRead((uint8_t)PIN_USB_SENSE) == HIGH;
}
static void boostEn(bool on) {
  if (PIN_BOOST_EN >= 0) digitalWrite((uint8_t)PIN_BOOST_EN, on ? HIGH : LOW);
}

static float readMa(uint16_t sampleMs) {
  uint32_t sum_mV = 0, n = 0;
  const uint32_t t0 = millis();
  while (millis() - t0 < sampleMs) { sum_mV += analogReadMilliVolts(PIN_CURRENT_ADC); n++; }
  return n ? (float(sum_mV) / n) / SENSE_V_PER_A : 0.0f;   // mV / (V/A) = mA
}
// "sat!" = at/above the measurement ceiling — real current is HIGHER. On BOTH
// boards the ESP32 ADC pin (~3.3 V full scale = ~1.1 A at 3.0 V/A) pins before
// the INA180 does; bench-confirmed readings plateau at ~1100 mA while an
// inline USB meter showed ~1.9 A of real drive current at 80-85% duty.
static const char* satFlag(float ma) { return ma >= SENSE_SAT_MA * 0.97f ? " sat!" : ""; }

static float readVbatt() {
  if (PIN_BATT_ADC < 0) return 0.0f;
  uint32_t sum_mV = 0;
  for (uint8_t i = 0; i < 16; i++) sum_mV += analogReadMilliVolts((uint8_t)PIN_BATT_ADC);
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
  boostEn(d > 0);
  if (d > 0) delayMicroseconds(500);
  ledcWrite(PIN_MIST_PWM, d);
  Serial.printf("[DUTY] %3u/255 = %5.1f%%\n", d, d * 100.0f / 255.0f);
}

static void saveLog() { prefs.putBytes("blog", &g_log, sizeof(g_log)); }

static void printLogTable(const char* header) {
  Serial.println();
  Serial.println(header);
  Serial.println("[LOG]  duty%   drive-mA   Vbatt");
  for (uint8_t i = 0; i < g_log.count; i++)
    Serial.printf("[LOG]  %5.1f   %7.1f%s   %5.2f V\n",
                  g_log.duty[i] * 100.0f / 255.0f, g_log.ma[i],
                  satFlag(g_log.ma[i]), g_log.vb[i]);
  const char* why = g_log.abortReason == 1 ? "current limit (D1 rating)"
                  : g_log.abortReason == 2 ? "cell sagged (LDO dropout guard)"
                  : g_log.abortReason == 3 ? "USB replugged mid-sweep"
                  : g_log.abortReason == 4 ? "sensor saturated (current unmeasurable = uncontrollable)"
                  : g_log.done             ? "completed full range"
                                           : "DID NOT FINISH (reset mid-sweep?)";
  Serial.printf("[LOG] end: %s\n", why);
}

// ---------------- attended (serial) sweep ----------------
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
    boostEn(true);
    ledcWrite(PIN_MIST_PWM, d);
    delay(SWEEP_SETTLE_MS);
    ma[i] = readMa(SWEEP_DWELL_MS - SWEEP_SETTLE_MS);
    ran = i + 1;
    Serial.printf("[SWEEP]  %2u/%u   %3u   %5.1f%%   %6.1f mA%s\n",
                  i, SWEEP_STEPS, d, d * 100.0f / 255.0f, ma[i], satFlag(ma[i]));
    if (ma[i] >= SENSE_SAT_MA) {
      Serial.println("[SWEEP] current sensor SATURATED — real current is higher than");
      Serial.println("[SWEEP] the board can measure or limit. Stopping for safety.");
      break;
    }
    if (ma[i] > limitMa()) {
      Serial.printf("[SWEEP] %.0f mA > %.0f mA limit — stopping. 'L' raises the limit\n"
                    "[SWEEP] (max 1200 mA = D1's rating). 'U' arms an unattended sweep\n"
                    "[SWEEP] you can run from a beefy wall charger.\n",
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
    Serial.printf("%5.1f, %.1f%s\n", duties[i] * 100.0f / 255.0f, ma[i], satFlag(ma[i]));
}

// ---------------- unattended sweep (battery 'B' / 'U') ----------------
// Runs with serial dead. Every step lands in NVS before the next begins, so
// the table survives a brownout, a power cut, or a cell collapse.
// startedOnBattery: a 'B' sweep aborts if USB returns mid-run (its data mixes
// sources otherwise). The cell-voltage floor is checked DYNAMICALLY — if a
// brick folds mid-'U'-sweep and the mux hands off to the cell, the floor
// guard picks up from that step (a V0.4 sweep can change sources mid-run).
static void unattendedSweep(bool startedOnBattery) {
  memset(&g_log, 0, sizeof(g_log));
  g_log.magic  = SWEEP_LOG_MAGIC;
  g_log.unread = 1;

  // Row 0: duty-0 baseline (resting-ish Vbatt; the sag in later rows vs this
  // row is the cell's internal-resistance story).
  delay(300);
  g_log.duty[0] = 0;
  g_log.ma[0]   = readMa(200);
  g_log.vb[0]   = readVbatt();
  g_log.count   = 1;
  saveLog();

  for (uint8_t i = 1; i < USWEEP_MAX_STEPS; i++) {
    uint16_t d16 = USWEEP_START + (uint16_t)(i - 1) * USWEEP_STEP;
    const uint8_t d = d16 > DUTY_HARD_MAX ? DUTY_HARD_MAX : (uint8_t)d16;

    boostEn(true);
    ledcWrite(PIN_MIST_PWM, d);
    delay(USWEEP_SETTLE_MS);
    const float ma = readMa(USWEEP_SAMPLE_MS);
    const float vb = readVbatt();               // still under load

    g_log.duty[i] = d; g_log.ma[i] = ma; g_log.vb[i] = vb;
    g_log.count = i + 1;

    const bool onCellNow = PIN_BATT_ADC >= 0 && !onUsb();
    if      (ma >= SENSE_SAT_MA)                      g_log.abortReason = 4;
    else if (ma > limitMa())                          g_log.abortReason = 1;
    else if (onCellNow && vb < USWEEP_MIN_VBATT)      g_log.abortReason = 2;
    else if (startedOnBattery && onUsb())             g_log.abortReason = 3;
    saveLog();                                   // step is safe in flash NOW

    ledcWrite(PIN_MIST_PWM, 0);                  // off-cooldown for L1
    delay(USWEEP_COOLDOWN_MS);

    if (g_log.abortReason || d == DUTY_HARD_MAX) break;
  }
  g_log.done = 1;
  saveLog();
  ledcWrite(PIN_MIST_PWM, 0);
  boostEn(false);
}

static void printHelp() {
  Serial.printf("[HELP] w=serial sweep  U=arm unattended sweep  L=limit (now %.0f mA)\n", limitMa());
#if defined(BOARD_BATTERY_KIT_V04)
  Serial.println("[HELP] B=arm battery sweep (unplug USB after arming)");
#endif
  Serial.println("[HELP] r=reprint last stored sweep table");
  Serial.println("[HELP] 0..9=duty n*10%  +/-=nudge  s=stream  x=off  h=help");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(PIN_CURRENT_ADC, INPUT);
  if (PIN_BATT_ADC  >= 0) pinMode((uint8_t)PIN_BATT_ADC, INPUT);
  if (PIN_USB_SENSE >= 0) pinMode((uint8_t)PIN_USB_SENSE, INPUT); // external divider — no pull!
  if (PIN_BOOST_EN  >= 0) { pinMode((uint8_t)PIN_BOOST_EN, OUTPUT); boostEn(false); }
  ledcAttach(PIN_MIST_PWM, MIST_FREQ_HZ, PWM_RES_BITS);
  ledcWrite(PIN_MIST_PWM, 0);

  prefs.begin("dsweep");
  g_limitIdx = prefs.getUChar("lim", 0);
  if (g_limitIdx >= sizeof(LIMIT_STEPS_MA) / sizeof(LIMIT_STEPS_MA[0])) g_limitIdx = 0;

  Serial.println("==============================================");
#if defined(BOARD_BATTERY_KIT_V04)
  Serial.println(" Duty Sweep v3 - Battery Kit V0.4");
#else
  Serial.println(" Duty Sweep v3 - Extension Kit V0.1");
#endif
  Serial.println("==============================================");
  const esp_reset_reason_t rr = esp_reset_reason();
  Serial.printf("[BOOT] reset: %s\n",
                rr == ESP_RST_BROWNOUT ? "BROWNOUT (supply sagged under load!)"
                : rr == ESP_RST_POWERON ? "power-on"
                : rr == ESP_RST_SW      ? "software" : "other");

  // Armed for a sweep-on-boot ('U')? Run it now — even with no serial there.
  // Exception: booting on BATTERY (a reset mid-transfer) is not the intended
  // supply — convert to "wait for USB to arrive" instead of sweeping a
  // surprise source.
  if (prefs.getUChar("armU", 0) && !onUsb()) {
    prefs.putUChar("armU", 0);
    g_armUPending = true; g_sawUsbDrop = true;   // sweep when USB (brick) shows up
    Serial.println("[U] armed, booted on battery — will sweep when USB power arrives.");
  } else if (prefs.getUChar("armU", 0)) {
    prefs.putUChar("armU", 0);                   // one shot, even if we crash
    Serial.printf("[U] armed sweep: starting in %lu s (x to cancel)...\n",
                  (unsigned long)(USWEEP_BOOT_DELAY_MS / 1000));
    const uint32_t t0 = millis();
    bool cancelled = false;
    while (millis() - t0 < USWEEP_BOOT_DELAY_MS)
      if (Serial.available() && Serial.read() == 'x') { cancelled = true; break; }
    if (!cancelled) {
      unattendedSweep(!onUsb());                 // startedOnBattery if no USB now
      // On a wall brick nobody sees this print — the table stays flagged
      // unread in flash and replays when the board is back on the computer.
      printLogTable("[LOG] ==== unattended sweep results ====");
    } else Serial.println("[U] cancelled.");
  }

  // Load the black box but do NOT print-and-mark-read at boot: on native-USB
  // boards setup() runs before any serial monitor attaches, so a boot-time
  // replay prints into the void and flags the table read unseen (that also
  // happened right after a 'U' sweep on a wall brick — same boot, same bug).
  // loop() replays once a monitor is actually connected; 'r' reprints anytime.
  prefs.getBytes("blog", &g_log, sizeof(g_log));

  Serial.println("[!] Disc must be IN WATER before you start.");
  printHelp();
}

void loop() {
  // 'U' armed while a cell keeps the board alive: no reboot will come, so
  // trigger when USB drops (unplug from computer) and returns (wall brick).
  if (g_armUPending && PIN_USB_SENSE >= 0) {
    if (!onUsb()) g_sawUsbDrop = true;
    else if (g_sawUsbDrop) {
      g_armUPending = false; g_sawUsbDrop = false;
      prefs.putUChar("armU", 0);
      delay(USWEEP_BOOT_DELAY_MS);               // settle on the new supply
      unattendedSweep(false);                    // started on USB (brick)
    }
  }

#if defined(BOARD_BATTERY_KIT_V04)
  // Armed battery sweep: wait for the unplug, then run unattended.
  if (g_armedBatt) {
    if (!onUsb()) {
      delay(2000);
      unattendedSweep(true);
      g_armedBatt = false;                       // table replays on replug
    } else if (millis() - g_armedAtMs > BSWEEP_ARM_TIMEOUT_MS) {
      g_armedBatt = false;
      Serial.println("[BATT] arm timed out (30 s) — press B and unplug sooner.");
    }
  }
#endif

  // Replay results once a serial MONITOR is attached (bool(Serial) = host
  // opened the port on native-USB boards) and a table is unread. This is the
  // only place unread gets cleared — a print nobody could see doesn't count.
  static uint32_t lastReplayCheck = 0;
  if (millis() - lastReplayCheck > 1000) {
    lastReplayCheck = millis();
    if (g_log.unread && g_log.count && (bool)Serial) {
      printLogTable("[LOG] ==== sweep results ====");
      if (!g_log.done)
        Serial.println("[LOG] sweep did not finish — power was lost or the board "
                       "reset mid-sweep; the wall is at or just past the last saved row.");
      g_log.unread = 0;
      saveLog();
    }
  }

  while (Serial.available()) {
    const char ch = Serial.read();
    if      (ch == 'w') sweep();
#if defined(BOARD_BATTERY_KIT_V04)
    else if (ch == 'B') {
      if (!onUsb()) { Serial.println("[BATT] already on battery — plug USB in, then arm."); }
      else {
        g_armUPending = false; g_sawUsbDrop = false; prefs.putUChar("armU", 0);
        g_armedBatt = true; g_armedAtMs = millis();
        Serial.printf("[BATT] ARMED (limit %.0f mA, floor %.2f V, duty 50->90%%).\n",
                      limitMa(), USWEEP_MIN_VBATT);
        Serial.println("[BATT] Unplug USB within 30 s. Watch the mist (~2.5 s/step).");
        Serial.println("[BATT] Replug when the mist stops — the table replays here.");
      }
    }
#endif
    else if (ch == 'U') {
      g_armedBatt = false;                       // one arm mode at a time
      prefs.putUChar("armU", 1);
      g_armUPending = true; g_sawUsbDrop = false;
      Serial.printf("[U] ARMED (limit %.0f mA, duty 50->90%%).\n", limitMa());
      Serial.println("[U] Unplug and move the board to the wall charger. No cell: it");
      Serial.println("[U] boots there, waits 8 s, sweeps. Cell attached: it stays alive");
      Serial.println("[U] and starts 8 s after USB power returns. Either way the table");
      Serial.println("[U] logs to flash — plug back into the computer to read it.");
    }
    else if (ch == 'L') {
      g_limitIdx = (g_limitIdx + 1) % (sizeof(LIMIT_STEPS_MA) / sizeof(LIMIT_STEPS_MA[0]));
      prefs.putUChar("lim", g_limitIdx);         // survives reboot/power-cycle
      Serial.printf("[CFG] current limit -> %.0f mA%s\n", limitMa(),
                    g_limitIdx == 2 ? "  (D1's 1 A rating — short steps only, watch L1!)" : "");
    }
    else if (ch == 'r') {                        // reprint stored table, any state
      if (loadLog() && g_log.count)
        printLogTable("[LOG] ==== last sweep stored in flash ====");
      else Serial.println("[LOG] no sweep stored yet (or stored by an older sketch version).");
    }
    else if (ch == 'x') { g_armedBatt = false; g_armUPending = false;
                          g_sawUsbDrop = false; prefs.putUChar("armU", 0); setDuty(0); }
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
