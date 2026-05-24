// D7 white LED — "waiting" indicator. Dim when no container; off when docked.

#include "pins.h"
#include "config.h"

void statusLedInit() {
  ledcAttach(PIN_STATUS_LED, STATUS_LED_FREQ_HZ, STATUS_LED_RES);
  ledcWrite(PIN_STATUS_LED, 0);
}

void statusLedSet(bool dimOn) {
  ledcWrite(PIN_STATUS_LED, dimOn ? cfg.statusLedDimDuty : 0);
}

// Blocking blink used by the 5-tap mode-toggle path right before ESP.restart().
// Driven at full duty (not the dim "waiting" duty) so the confirmation is
// visually distinct from normal idle. enteringDemo=true → 3 fast pulses
// (~600 ms); false (entering config) → 2 slow pulses (~1000 ms).
void statusLedFlash(bool enteringDemo) {
  const uint16_t onMs  = enteringDemo ? 100 : 300;
  const uint16_t offMs = enteringDemo ? 100 : 200;
  const uint8_t  count = enteringDemo ? 3 : 2;
  for (uint8_t i = 0; i < count; ++i) {
    ledcWrite(PIN_STATUS_LED, 255);
    delay(onMs);
    ledcWrite(PIN_STATUS_LED, 0);
    delay(offMs);
  }
}
