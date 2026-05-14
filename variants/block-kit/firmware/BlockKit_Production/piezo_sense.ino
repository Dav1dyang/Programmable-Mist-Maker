// Current-sense classifier for disc-presence and water-level detection.
//
// Two explicit probes drive a fixed PWM duty for a short window, sample
// the ADC at full rate, and classify the mean current:
//
//   probeAtDuty(cfg.senseProbeDuty=10)        → above/below senseDiscPresentMa10x
//                                              → DISC_PRESENT vs DISC_MISSING
//   probeAtDuty(cfg.senseWaterProbeDuty=64)   → tri-class:
//      < senseDiscDisconnMidMa10x  → DISC_DISCONNECTED (snapped off mid-run)
//      < senseWaterLowMa10x        → WATER_LOW (start 5-min countdown)
//      ≥ senseWaterLowMa10x + hyst → WATER_OK
//
// Disc-presence probe runs on every reed Inserted event (blocking ~150 ms,
// invisible UX since the user just docked). Water-level probe runs every
// cfg.senseWaterCheckIntervalS during RUNNING (blocking ~100 ms, briefly
// holds mist at a fixed level — barely perceivable inside the wave's
// natural ±duty variation).

#include "pins.h"
#include "config.h"

extern float adcToMa(uint16_t raw);   // defined in current_sense.ino

// ---- Module state ----
static PiezoState g_piezoState     = PiezoState::UNKNOWN;
static float      g_lastProbeMa    = 0.0f;
static uint32_t   g_lastWaterCheckMs = 0;
static uint32_t   g_waterLowSinceMs  = 0;   // 0 = not in countdown

// Forward decls of mist-driver primitives we need.
extern void mistEnable(bool);
extern bool mistIsInhibited();

// Public API ---------------------------------------------------------------

PiezoState piezoState()                  { return g_piezoState; }
float      piezoLastProbeMa()            { return g_lastProbeMa; }
uint32_t   piezoWaterLowSinceMs()        { return g_waterLowSinceMs; }
void       piezoResetForNewDock()        { g_piezoState = PiezoState::UNKNOWN;
                                           g_waterLowSinceMs = 0;
                                           g_lastWaterCheckMs = 0; }

// Returns the seconds remaining in the WATER_LOW countdown, or 0 if no
// countdown is active. Used by the status JSON and the UI.
uint32_t piezoWaterCountdownS() {
  if (g_waterLowSinceMs == 0) return 0;
  const uint32_t elapsed = millis() - g_waterLowSinceMs;
  const uint32_t window  = uint32_t(cfg.senseWaterShutdownS) * 1000u;
  if (elapsed >= window) return 0;
  return (window - elapsed) / 1000u;
}

// Probe primitive: drive PWM at `duty` for settleMs + sampleMs, sample
// ADC during the sample window, return mean current in mA.
//
// Caller MUST have the boost rail engaged (i.e. mistEnable(true) earlier
// in the flow). We don't manipulate the boost here so back-to-back probes
// don't cycle the rail unnecessarily.
//
// Total time blocking: settleMs + sampleMs. analogRead on ESP32-C6 takes
// ~3-4 us, so we get ~250-300 samples per ms of sampleMs.
static float probeAtDuty(uint8_t duty, uint16_t settleMs, uint16_t sampleMs) {
  if (mistIsInhibited()) return 0.0f;  // boost rail not engaged — meaningless probe
  ledcWrite(PIN_MIST_PWM, duty);
  delay(settleMs);
  uint32_t sum = 0;
  uint32_t count = 0;
  const uint32_t startMs = millis();
  while (millis() - startMs < sampleMs) {
    sum += analogRead(PIN_CURRENT_ADC);
    count++;
  }
  if (count == 0) return 0.0f;
  return adcToMa(uint16_t(sum / count));
}

// Disc-presence probe — called from enterRunning() right after mistEnable.
// Blocks ~150 ms (100 ms settle + 50 ms sample). Sets g_piezoState.
// Returns true if disc was detected (caller should proceed with mist);
// false to abort (caller should NOT engage mist).
bool piezoSenseProbeOnInsert() {
  // Boost rail must be on; enterRunning() called mistEnable(true) before us.
  g_lastProbeMa = probeAtDuty(cfg.senseProbeDuty, 100, 50);
  const float thresh = float(cfg.senseDiscPresentMa10x) / 10.0f;
  Serial.printf("[SENSE] disc probe: %.1f mA at PWM=%u (threshold %.1f mA)\n",
                g_lastProbeMa, cfg.senseProbeDuty, thresh);
  if (g_lastProbeMa >= thresh) {
    g_piezoState = PiezoState::WATER_OK;   // optimistic until first water probe says otherwise
    return true;
  }
  g_piezoState = PiezoState::DISC_MISSING;
  Serial.println("[SENSE] disc missing — refusing to engage mist");
  return false;
}

// Classify a water-level probe reading. Mutates g_piezoState and the
// countdown timer. Called from piezoSensePeriodicWaterCheck() AND from
// piezoCalibrateWaterBaseline() so the threshold logic lives in one place.
static void classifyWaterReading(float ma) {
  g_lastProbeMa = ma;
  const float discDisconn = float(cfg.senseDiscDisconnMidMa10x) / 10.0f;
  const float waterLow    = float(cfg.senseWaterLowMa10x)       / 10.0f;
  const float hyst        = float(cfg.senseWaterHystMa10x)      / 10.0f;

  if (ma < discDisconn) {
    g_piezoState = PiezoState::DISC_DISCONNECTED;
    g_waterLowSinceMs = 0;
    Serial.printf("[SENSE] DISC_DISCONNECTED — current %.1f mA < %.1f mA\n", ma, discDisconn);
    return;
  }
  if (ma < waterLow) {
    if (g_piezoState != PiezoState::WATER_LOW && g_piezoState != PiezoState::WATER_DEPLETED) {
      g_waterLowSinceMs = millis();   // start countdown
      Serial.printf("[SENSE] WATER_LOW — countdown to shutdown begins (%.1f mA)\n", ma);
    }
    // Check countdown expiry.
    if (g_waterLowSinceMs > 0 &&
        millis() - g_waterLowSinceMs >= uint32_t(cfg.senseWaterShutdownS) * 1000u) {
      g_piezoState = PiezoState::WATER_DEPLETED;
      Serial.println("[SENSE] WATER_DEPLETED — countdown expired, mist will hard-stop");
    } else {
      g_piezoState = PiezoState::WATER_LOW;
    }
    return;
  }
  // ma >= waterLow. Apply hysteresis: only recover if comfortably above.
  if (g_piezoState == PiezoState::WATER_LOW && ma < (waterLow + hyst)) {
    return;  // stay LOW, don't flap
  }
  if (g_piezoState != PiezoState::WATER_OK) {
    Serial.printf("[SENSE] WATER_OK (%.1f mA)\n", ma);
  }
  g_piezoState = PiezoState::WATER_OK;
  g_waterLowSinceMs = 0;
}

// Periodic water-level check — called from loop(). Returns early most of
// the time; once per cfg.senseWaterCheckIntervalS does a brief probe at
// senseWaterProbeDuty.
//
// Should only be invoked when state == RUNNING (caller's responsibility).
void piezoSensePeriodicWaterCheck() {
  const uint32_t now = millis();
  // First call after entering RUNNING — schedule next, no probe yet.
  if (g_lastWaterCheckMs == 0) { g_lastWaterCheckMs = now; return; }
  if (now - g_lastWaterCheckMs < uint32_t(cfg.senseWaterCheckIntervalS) * 1000u) return;
  g_lastWaterCheckMs = now;
  const float ma = probeAtDuty(cfg.senseWaterProbeDuty, 50, 50);
  classifyWaterReading(ma);
}

// "Calibrate now" — user clicks the button in the web UI while the device
// is in a known-good state (water present, mist running normally). We
// probe at senseWaterProbeDuty, record the reading, and recommend a new
// low-water threshold at 85% of recorded. Returns the recorded mA.
float piezoCalibrateWaterBaseline() {
  const float ma = probeAtDuty(cfg.senseWaterProbeDuty, 100, 100);
  Serial.printf("[SENSE] calibrate: %.1f mA at PWM=%u — recommended low threshold %.1f mA\n",
                ma, cfg.senseWaterProbeDuty, ma * 0.85f);
  // Caller decides whether to apply via /api/config POST.
  return ma;
}
