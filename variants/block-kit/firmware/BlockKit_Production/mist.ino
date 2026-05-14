// Mist drive — 108.7 kHz PWM on D0, gated by D3 boost enable.
//
// mistApply(level) gates the 5 V boost rail automatically:
//   level 0 + boost ON  → PWM 0, D0 LOW, boost OFF.
//   level > 0 + boost OFF → boost ON, 500 µs settle, then PWM.
//   level > 0 + boost ON  → update duty (level 255 = cfg.mistDutyMax).
// mistHardStop() bypasses the smoother for safety (container lift, OTA).

#include "pins.h"
#include "config.h"

static bool g_mistBoostOn   = false;
// Set by mistHardStop(), cleared by mistEnable(true). While set, mistApply()
// is a no-op. This is the lock that prevents the LED-fade smoother (which
// still feeds non-zero `level` during the fade) from re-engaging the boost
// rail seconds after the container has been lifted.
static bool g_mistInhibited = false;

void mistInit() {
  pinMode(PIN_BOOST_EN, OUTPUT);
  digitalWrite(PIN_BOOST_EN, LOW);

  // Arduino-ESP32 v3.x LEDC API: attach to a pin directly with freq + resolution.
  ledcAttach(PIN_MIST_PWM, MIST_FREQ_HZ, MIST_PWM_RES);
  ledcWrite(PIN_MIST_PWM, 0);
}

static inline uint8_t levelToDuty(uint8_t level) {
  // Map 0..255 user level to cfg.mistDutyMin..cfg.mistDutyMax. The "min"
  // is the floor while level>0 — set to 0 in cfg to restore the original
  // pure-linear scaling. Without it, very low user levels mapped to a
  // PWM duty below the piezo's effective threshold.
  const uint16_t scaled = (uint16_t(level) * cfg.mistDutyMax) / 255u;
  return uint8_t(scaled < cfg.mistDutyMin ? cfg.mistDutyMin : scaled);
}

// Update mist output based on the current smoothed level. Called once per
// main-loop iteration. No-op while inhibited (post hard-stop until re-enabled).
void mistApply(uint8_t level) {
  if (g_mistInhibited) return;

  if (level == 0) {
    if (g_mistBoostOn) {
      ledcWrite(PIN_MIST_PWM, 0);
      digitalWrite(PIN_MIST_PWM, LOW);  // safety belt — force D0 low
      digitalWrite(PIN_BOOST_EN, LOW);
      g_mistBoostOn = false;
      Serial.println("[MIST] off (level=0)");
    }
    return;
  }

  if (!g_mistBoostOn) {
    digitalWrite(PIN_BOOST_EN, HIGH);
    delayMicroseconds(500);             // brief settle for the 5 V rail
    g_mistBoostOn = true;
    Serial.println("[MIST] on (boost up)");
  }
  ledcWrite(PIN_MIST_PWM, levelToDuty(level));
}

// Immediately stop mist regardless of the smoother — used on container lift
// for safety. Also locks mistApply() as a no-op until mistEnable(true) is
// called, so the LED ring's gradual fade-down doesn't re-engage the boost.
// Idempotent: if mist was already off we just (re)assert the inhibit flag
// without churning the pins.
void mistHardStop() {
  if (g_mistBoostOn) {
    ledcWrite(PIN_MIST_PWM, 0);
    digitalWrite(PIN_MIST_PWM, LOW);
    digitalWrite(PIN_BOOST_EN, LOW);
    g_mistBoostOn = false;
    Serial.println("[MIST] hard-stop");
  }
  g_mistInhibited = true;
}

// Re-arm the mist path so the next mistApply(level>0) can engage the boost.
// Called by the main loop when entering RUNNING.
void mistEnable(bool enabled) {
  if (enabled) g_mistInhibited = false;
  else         mistHardStop();
}

bool mistIsRunning()  { return g_mistBoostOn; }
bool mistIsInhibited() { return g_mistInhibited; }
