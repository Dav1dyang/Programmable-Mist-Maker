// Current-sense classifier for disc-presence and water-level detection.
//
// Two probe types share probeAtDuty(): a low-PWM disc-presence probe used by
// piezoAutoProbeForDisc() in reed-disabled mode, and a higher-PWM water-level
// probe run every senseWaterCheckIntervalS during RUNNING. Reed-enabled mode
// (default) skips disc-presence; the water probe still catches DISC_DISCONNECTED.

#include "pins.h"
#include "config.h"

extern float adcToMa(uint16_t raw);
extern void mistEnable(bool);
extern bool mistIsInhibited();
extern bool mistIsRunning();
extern void mistBoostOnForProbe();
extern void mistBoostOffForProbe();
extern uint8_t waveIntensityAtPiezo(uint32_t now);

static PiezoState g_piezoState     = PiezoState::UNKNOWN;
static float      g_lastProbeMa    = 0.0f;
static uint32_t   g_lastWaterCheckMs = 0;
static uint32_t   g_waterLowSinceMs  = 0;   // 0 = no countdown active

PiezoState piezoState()       { return g_piezoState; }
float      piezoLastProbeMa() { return g_lastProbeMa; }
void       piezoResetForNewDock() {
  g_piezoState = PiezoState::UNKNOWN;
  g_waterLowSinceMs = 0;
  g_lastWaterCheckMs = 0;
}

uint32_t piezoWaterCountdownS() {
  if (g_waterLowSinceMs == 0) return 0;
  const uint32_t elapsed = millis() - g_waterLowSinceMs;
  const uint32_t window  = uint32_t(cfg.senseWaterShutdownS) * 1000u;
  if (elapsed >= window) return 0;
  return (window - elapsed) / 1000u;
}

// Drive PWM at `duty` for settleMs, then sample ADC for sampleMs and return
// mean mA. Caller is responsible for the boost rail being engaged — we don't
// cycle it here so back-to-back probes don't churn the converter.
static float probeAtDuty(uint8_t duty, uint16_t settleMs, uint16_t sampleMs) {
  if (mistIsInhibited()) return 0.0f;
  ledcWrite(PIN_MIST_PWM, duty);
  delay(settleMs);
  uint32_t sum = 0, count = 0;
  const uint32_t startMs = millis();
  while (millis() - startMs < sampleMs) {
    sum += analogRead(PIN_CURRENT_ADC);
    count++;
  }
  if (count == 0) return 0.0f;
  return adcToMa(uint16_t(sum / count));
}

// Threshold logic lives here so it's shared between the periodic probe and the
// calibrate-now path. Mutates g_piezoState + the countdown timer.
static void classifyWaterReading(float ma) {
  g_lastProbeMa = ma;
  const float discDisconn = float(cfg.senseDiscDisconnMidMa10x) / 10.0f;
  const float waterLow    = float(cfg.senseWaterLowMa10x)       / 10.0f;
  const float hyst        = float(cfg.senseWaterHystMa10x)      / 10.0f;

  if (ma < discDisconn) {
    g_piezoState = PiezoState::DISC_DISCONNECTED;
    g_waterLowSinceMs = 0;
    logPrintf("[SENSE] DISC_DISCONNECTED — %.1f mA < %.1f\n", ma, discDisconn);
    return;
  }
  if (ma < waterLow) {
    if (g_piezoState != PiezoState::WATER_LOW && g_piezoState != PiezoState::WATER_DEPLETED) {
      g_waterLowSinceMs = millis();
      logPrintf("[SENSE] WATER_LOW — countdown begins (%.1f mA)\n", ma);
    }
    if (g_waterLowSinceMs > 0 &&
        millis() - g_waterLowSinceMs >= uint32_t(cfg.senseWaterShutdownS) * 1000u) {
      g_piezoState = PiezoState::WATER_DEPLETED;
      logPrintln("[SENSE] WATER_DEPLETED — countdown expired");
    } else {
      g_piezoState = PiezoState::WATER_LOW;
    }
    return;
  }
  // ma >= waterLow. Hysteresis: don't flap back to OK until comfortably above.
  if (g_piezoState == PiezoState::WATER_LOW && ma < (waterLow + hyst)) return;
  if (g_piezoState != PiezoState::WATER_OK) {
    Serial.printf("[SENSE] WATER_OK (%.1f mA)\n", ma);
  }
  g_piezoState = PiezoState::WATER_OK;
  g_waterLowSinceMs = 0;
}

// Caller's responsibility to invoke only when state == RUNNING.
//
// Skips the probe if the boost rail isn't actually engaged — happens when the
// user has set userLevel=0 in RUNNING (mist paused but state held). Probing
// against a powered-down rail reads near-zero current and would falsely
// classify a still-docked disc as DISC_DISCONNECTED.
void piezoSensePeriodicWaterCheck() {
  const uint32_t now = millis();
  if (g_lastWaterCheckMs == 0) { g_lastWaterCheckMs = now; return; }
  if (now - g_lastWaterCheckMs < uint32_t(cfg.senseWaterCheckIntervalS) * 1000u) return;
  g_lastWaterCheckMs = now;
  if (!mistIsRunning()) return;   // boost off — meaningful probe impossible
  classifyWaterReading(probeAtDuty(cfg.senseWaterProbeDuty, 50, 50));
}

// Fast disc-presence check during RUNNING — only relevant when senseUseAsReed
// is enabled, since reed-removal events are ignored in that mode. Probes at
// PWM=10 every senseAutoProbeIntervalS; below threshold → DISC_DISCONNECTED.
//
// To keep the 5 s cadence but hide the visible mist dip + LED freeze, the
// probe is "armed" on each interval and then deferred until the wave's
// natural trough (gauss at piezo ≈ 0) — that's already the dimmest moment,
// so dropping PWM to 10 for 20 ms there is almost imperceptible. Bounded by
// a 2 s timeout so the cadence doesn't drift if a trough is slow to arrive.
void piezoSensePeriodicDiscCheck() {
  static uint32_t lastTickMs = 0;
  static uint32_t armedAtMs  = 0;   // 0 = not armed
  const uint32_t now = millis();
  if (lastTickMs == 0) { lastTickMs = now; return; }
  if (now - lastTickMs >= uint32_t(cfg.senseAutoProbeIntervalS) * 1000u) {
    lastTickMs = now;
    armedAtMs = now;                // arm — actual probe waits for trough
  }
  if (armedAtMs == 0) return;
  if (!mistIsRunning()) { armedAtMs = 0; return; }

  // Threshold ≈ bottom 10% of the gauss range. Most of each wave cycle is
  // below this, so we almost always probe in-trough within the 2 s budget.
  const uint8_t gauss = waveIntensityAtPiezo(now);
  if (gauss > 24 && (now - armedAtMs) < 2000u) return;
  armedAtMs = 0;

  // Shorter probe (20 ms total) than the IDLE-mode auto-probe: the boost
  // rail is already engaged and steady, so 10 ms is plenty of settle for
  // a PWM duty change at 108.7 kHz. 10 ms of sampling at the ESP32-C6 ADC
  // rate is still ~3000 samples — overkill for a stable mean.
  const float ma = probeAtDuty(cfg.senseProbeDuty, 10, 10);
  const float thresh = float(cfg.senseDiscPresentMa10x) / 10.0f;
  g_lastProbeMa = ma;
  Serial.printf("[SENSE] removal-check: %.1f mA (thresh %.1f)\n", ma, thresh);
  if (ma < thresh) g_piezoState = PiezoState::DISC_DISCONNECTED;
}

// Called from the web "Calibrate water" button. Returns 0.0 if the rail isn't
// engaged (the caller — web handler — should reject and surface an error).
// Caller decides whether to apply the recommended threshold via /api/config POST.
float piezoCalibrateWaterBaseline() {
  if (!mistIsRunning()) {
    logPrintln("[SENSE] calibrate skipped — mist not running");
    return 0.0f;
  }
  const float ma = probeAtDuty(cfg.senseWaterProbeDuty, 100, 100);
  logPrintf("[SENSE] calibrate: %.1f mA — recommended low threshold %.1f mA\n",
            ma, ma * 0.85f);
  return ma;
}

// Auto-probe for disc presence in IDLE — used only when cfg.senseUseAsReed=true.
// Returns true when a fresh disc is detected; caller should enterRunning(). On
// true return, the boost rail is LEFT ENGAGED so the smoother can take over
// without a re-settle gap. In a fault state (WATER_DEPLETED / DISC_DISCONNECTED)
// a probe with no disc clears the fault — the user lifted the dispenser.
bool piezoAutoProbeForDisc() {
  static uint32_t lastTickMs = 0;
  const uint32_t now = millis();
  if (now - lastTickMs < uint32_t(cfg.senseAutoProbeIntervalS) * 1000u) return false;
  lastTickMs = now;

  const bool inFault = (g_piezoState == PiezoState::WATER_DEPLETED ||
                        g_piezoState == PiezoState::DISC_DISCONNECTED);

  mistBoostOnForProbe();
  const float ma = probeAtDuty(cfg.senseProbeDuty, 100, 50);
  const float thresh = float(cfg.senseDiscPresentMa10x) / 10.0f;
  const bool present = (ma >= thresh);
  g_lastProbeMa = ma;

  Serial.printf("[SENSE] auto-probe: %.1f mA (thresh %.1f, fault=%d, present=%d)\n",
                ma, thresh, int(inFault), int(present));

  if (inFault) {
    if (!present) {
      logPrintln("[SENSE] post-fault lift detected");
      piezoResetForNewDock();
    }
    mistBoostOffForProbe();
    return false;
  }

  if (present) {
    g_piezoState = PiezoState::WATER_OK;   // optimistic until first water probe
    return true;                            // boost stays on for the smoother
  }
  g_piezoState = PiezoState::DISC_MISSING;
  mistBoostOffForProbe();
  return false;
}
