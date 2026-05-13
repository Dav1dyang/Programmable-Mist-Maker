// D7 white LED — simple "waiting" indicator.
//
// On when no container is docked (dim, ~10 % brightness). Off the moment the
// container is dropped onto the dock (the 14-LED ring takes over the user's
// attention). No breathing, no fade — a single LED doesn't need ceremony.

#include "pins.h"

void statusLedInit() {
  ledcAttach(PIN_STATUS_LED, STATUS_LED_FREQ_HZ, STATUS_LED_RES);
  ledcWrite(PIN_STATUS_LED, 0);
}

void statusLedSet(bool dimOn) {
  ledcWrite(PIN_STATUS_LED, dimOn ? STATUS_LED_DIM_DUTY : 0);
}
