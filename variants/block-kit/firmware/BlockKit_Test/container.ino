// Reed switch on D1 with asymmetric debounce.
// Insert (LOW edge) is gated by REED_INSERT_DWELL_MS to avoid mist rapid-cycling
// when the user is sliding the container or cleaning. Removal (HIGH edge) is
// faster so shut-off isn't sluggish.

#include "pins.h"

static bool     g_containerPresent  = false;
static bool     g_lastRawPresent    = false;
static uint32_t g_edgeStartMs       = 0;

void containerInit() {
  pinMode(PIN_REED, INPUT_PULLUP);
  // Always seed the committed state as "absent" so a container that's already
  // docked at boot is treated as a freshly arriving edge — the 500 ms insert
  // dwell still applies, which is the right safety behavior since we don't
  // know how long the magnet has been there.
  g_lastRawPresent   = (digitalRead(PIN_REED) == LOW);
  g_containerPresent = false;
  g_edgeStartMs      = millis();
  Serial.print("[REED] init raw_present=");
  Serial.println(g_lastRawPresent ? "1" : "0");
}

// Returns the debounced "is a container docked?" state. Cheap; call freely.
bool containerIsPresent() {
  return g_containerPresent;
}

// Poll the reed once. Returns Inserted/Removed on a confirmed debounced edge,
// or None otherwise. Call from the main loop every iteration.
ContainerEvent containerPoll() {
  const bool raw = (digitalRead(PIN_REED) == LOW);
  const uint32_t now = millis();

  // Edge restart: any change in raw read resets the dwell timer.
  if (raw != g_lastRawPresent) {
    g_lastRawPresent = raw;
    g_edgeStartMs = now;
    return ContainerEvent::None;
  }

  // Stable. If we already match the committed state, nothing to do.
  if (raw == g_containerPresent) return ContainerEvent::None;

  // Stable but different from committed -> check dwell.
  const uint16_t dwell = raw ? REED_INSERT_DWELL_MS : REED_REMOVE_DWELL_MS;
  if (now - g_edgeStartMs < dwell) return ContainerEvent::None;

  g_containerPresent = raw;
  if (raw) {
    Serial.println("[REED] inserted");
    return ContainerEvent::Inserted;
  } else {
    Serial.println("[REED] removed");
    return ContainerEvent::Removed;
  }
}
