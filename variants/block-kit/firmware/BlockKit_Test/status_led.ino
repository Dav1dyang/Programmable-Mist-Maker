// D7 white LED — slow breathing pulse.
//
// Uses the LED-driver swirl's 64-entry sine LUT (declared in led_driver.ino)
// indirectly via a local sin^2-style ramp: brightness = (1 + sin) / 2.
// Active only in IDLE; the main loop calls statusLedSet(true|false) on state
// transitions.

#include "pins.h"

// SINE_LUT is private to led_driver.ino, so we keep a tiny local LUT here to
// preserve the "one responsibility per file" rule. 16 entries is plenty for a
// 3 s breath cycle at 30 fps.
static const uint8_t BREATH_LUT[16] = {
  0, 5, 19, 41, 71, 105, 141, 175, 205, 226, 240, 247, 247, 240, 226, 205,
};

static bool     g_breathOn   = false;
static uint32_t g_lastTickMs = 0;

void statusLedInit() {
  ledcAttach(PIN_STATUS_LED, STATUS_LED_FREQ_HZ, STATUS_LED_RES);
  ledcWrite(PIN_STATUS_LED, 0);
}

// Enable or disable the breathing animation. Disabling turns the LED fully off.
void statusLedSet(bool breathing) {
  g_breathOn = breathing;
  if (!breathing) {
    ledcWrite(PIN_STATUS_LED, 0);
  }
}

void statusLedTick() {
  if (!g_breathOn) return;
  const uint32_t now = millis();
  if (now - g_lastTickMs < 30) return;  // ~33 fps is plenty for a slow breath
  g_lastTickMs = now;

  const uint8_t idx = uint8_t(((now * 16u) / STATUS_LED_BREATH_PERIOD_MS) & 0x0F);
  // Scale 0..247 to 0..STATUS_LED_BREATH_MAX.
  const uint32_t scaled = (uint32_t(BREATH_LUT[idx]) * STATUS_LED_BREATH_MAX) / 247u;
  ledcWrite(PIN_STATUS_LED, uint8_t(scaled));
}
