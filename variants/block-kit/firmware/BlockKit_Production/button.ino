// Button on D6 (active-HIGH, 10k pull-down on PCB).
// Emits ShortPress / LongPressStart / LongPressTick / LongPressEnd events.
// The main loop owns dim-direction; this file just reports.

#include "pins.h"
#include "config.h"

static bool     g_lastRaw         = false;  // last raw read
static bool     g_debounced       = false;  // committed debounced state
static uint32_t g_lastChangeMs    = 0;
static uint32_t g_pressStartMs    = 0;
static bool     g_longActive      = false;
static uint32_t g_btnLastTickMs      = 0;

void buttonInit() {
  // PCB has an external 10 k pull-down on D6. Enabling INPUT_PULLDOWN in
  // addition gives belt-and-braces protection: if the external pull-down has
  // a cold solder joint or the pin floats during early boot, the internal
  // ~45 k pull-down still keeps the input LOW until the button is pressed.
  // The two pull-downs in parallel still register a clean HIGH on press
  // (button pulls D6 to 3V3 through the switch, easily overcoming both).
  pinMode(PIN_BUTTON, INPUT_PULLDOWN);
}

ButtonEvent buttonPoll() {
  const bool raw = (digitalRead(PIN_BUTTON) == HIGH);
  const uint32_t now = millis();

  if (raw != g_lastRaw) {
    g_lastRaw = raw;
    g_lastChangeMs = now;
    return ButtonEvent::None;
  }

  // Debounce gate.
  if (now - g_lastChangeMs < cfg.buttonDebounceMs) return ButtonEvent::None;
  if (raw == g_debounced) {
    // Still pressed — emit long-press start once the threshold passes, then ticks.
    if (g_debounced) {
      if (!g_longActive && (now - g_pressStartMs >= cfg.buttonLongPressMs)) {
        g_longActive = true;
        g_btnLastTickMs = now;
        return ButtonEvent::LongPressStart;
      }
      if (g_longActive && (now - g_btnLastTickMs >= cfg.buttonLongTickMs)) {
        g_btnLastTickMs = now;
        return ButtonEvent::LongPressTick;
      }
    }
    return ButtonEvent::None;
  }

  // Committed state changes here.
  g_debounced = raw;
  if (raw) {
    // Press down.
    g_pressStartMs = now;
    g_longActive = false;
    return ButtonEvent::None;
  }

  // Release.
  if (g_longActive) {
    g_longActive = false;
    return ButtonEvent::LongPressEnd;
  }
  return ButtonEvent::ShortPress;
}
