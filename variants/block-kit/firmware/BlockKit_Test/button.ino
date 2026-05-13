// Button on D6 (active-HIGH, 10k pull-down on PCB).
// Emits ShortPress / LongPressStart / LongPressTick / LongPressEnd events.
// The main loop owns dim-direction; this file just reports.

#include "pins.h"

static bool     g_lastRaw         = false;  // last raw read
static bool     g_debounced       = false;  // committed debounced state
static uint32_t g_lastChangeMs    = 0;
static uint32_t g_pressStartMs    = 0;
static bool     g_longActive      = false;
static uint32_t g_lastTickMs      = 0;

void buttonInit() {
  pinMode(PIN_BUTTON, INPUT);  // 10k pull-down is on the PCB
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
  if (now - g_lastChangeMs < BUTTON_DEBOUNCE_MS) return ButtonEvent::None;
  if (raw == g_debounced) {
    // Still pressed — emit long-press start once the threshold passes, then ticks.
    if (g_debounced) {
      if (!g_longActive && (now - g_pressStartMs >= BUTTON_LONGPRESS_MS)) {
        g_longActive = true;
        g_lastTickMs = now;
        return ButtonEvent::LongPressStart;
      }
      if (g_longActive && (now - g_lastTickMs >= BUTTON_LONGTICK_MS)) {
        g_lastTickMs = now;
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
