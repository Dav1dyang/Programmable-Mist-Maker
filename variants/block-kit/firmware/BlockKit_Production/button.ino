// Button on D6 (active-HIGH, 10k pull-down on PCB).
// Emits ShortPress / LongPressStart / LongPressTick / LongPressEnd events.
// The main loop owns dim-direction; this file just reports.

#include "pins.h"
#include "config.h"

static bool     g_buttonLastRaw         = false;  // last raw read
static bool     g_buttonState       = false;  // committed debounced state
static uint32_t g_lastChangeMs    = 0;
static uint32_t g_pressStartMs    = 0;
static bool     g_inLongPress      = false;
static uint32_t g_longPressTickMs      = 0;

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

  if (raw != g_buttonLastRaw) {
    g_buttonLastRaw = raw;
    g_lastChangeMs = now;
    return ButtonEvent::None;
  }

  if (now - g_lastChangeMs < cfg.buttonDebounceMs) return ButtonEvent::None;

  if (raw == g_buttonState) {
    // Still pressed — emit long-press start once threshold passes, then ticks.
    if (g_buttonState) {
      if (!g_inLongPress && (now - g_pressStartMs >= cfg.buttonLongPressMs)) {
        g_inLongPress = true;
        g_longPressTickMs = now;
        return ButtonEvent::LongPressStart;
      }
      if (g_inLongPress && (now - g_longPressTickMs >= cfg.buttonLongTickMs)) {
        g_longPressTickMs = now;
        return ButtonEvent::LongPressTick;
      }
    }
    return ButtonEvent::None;
  }

  g_buttonState = raw;
  if (raw) {
    g_pressStartMs = now;
    g_inLongPress = false;
    return ButtonEvent::None;
  }
  if (g_inLongPress) {
    g_inLongPress = false;
    return ButtonEvent::LongPressEnd;
  }
  return ButtonEvent::ShortPress;
}
