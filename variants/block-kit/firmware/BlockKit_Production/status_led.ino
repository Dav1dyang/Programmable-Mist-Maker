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
