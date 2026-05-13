// IS31FL3731 (Matrix B) driver + gentle upward-traveling swirl.
//
// Animation uses a 64-entry integer sine LUT — no sinf() in the hot path so
// the ESP32-C6 (no FPU) keeps deterministic per-tick timing.

#include <Adafruit_IS31FL3731.h>
#include "pins.h"

// Signed sine LUT, range -127..+127, 64 entries.
static const int8_t SINE_LUT[64] = {
    0,  12,  24,  37,  48,  60,  71,  81,  90,  98, 106, 112, 117, 122, 125, 126,
  127, 126, 125, 122, 117, 112, 106,  98,  90,  81,  71,  60,  48,  37,  24,  12,
    0, -12, -24, -37, -48, -60, -71, -81, -90, -98,-106,-112,-117,-122,-125,-126,
 -127,-126,-125,-122,-117,-112,-106, -98, -90, -81, -71, -60, -48, -37, -24, -12,
};

static Adafruit_IS31FL3731 g_is31;
static bool     g_ledReady       = false;
static bool     g_animEnabled    = true;
static uint8_t  g_overall        = LED_DEFAULT_OVERALL;
static uint8_t  g_contrast       = LED_DEFAULT_CONTRAST;
static uint16_t g_periodMs       = LED_DEFAULT_PERIOD_MS;
static uint32_t g_lastTickMs     = 0;

// Compute per-LED brightness for the current millis() and write the frame.
static void renderFrame() {
  const uint32_t t = millis();
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    int16_t bright = g_overall;
    if (g_animEnabled && g_contrast > 0) {
      // phase = (t / period + i / N) * 64, modulo 64
      const uint32_t phase = ((t * 64u) / g_periodMs + (uint32_t(i) * 64u) / LED_COUNT) & 63u;
      const int16_t delta = (int16_t(g_contrast) * SINE_LUT[phase]) >> 7;
      bright += delta;
    }
    if (bright < 0)   bright = 0;
    if (bright > 255) bright = 255;
    g_is31.drawPixel(LED_POSITIONS[i][0], LED_POSITIONS[i][1], uint8_t(bright));
  }
}

void ledInit() {
  // Wire.begin() is called once in the main sketch setup.
  g_ledReady = g_is31.begin(LED_IS31_ADDR);
  if (!g_ledReady) {
    Serial.println("[LED] IS31FL3731 not found");
    return;
  }
  // Block Kit populates the Matrix B side of the chip (CB1..CB9). David's
  // fork is expected to expose Matrix B addressing through `drawPixel(x,y,…)`
  // when the Matrix B mode is selected — if his fork's API differs, the
  // selection call goes here at bring-up.
  g_is31.clear();
  Serial.println("[LED] init ok");
}

// Call from the main loop. Cheap if not yet due.
void ledTick() {
  if (!g_ledReady) return;
  const uint32_t now = millis();
  if (now - g_lastTickMs < LED_TICK_MS) return;
  g_lastTickMs = now;
  renderFrame();
}

void ledAllOff() {
  if (!g_ledReady) return;
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    g_is31.drawPixel(LED_POSITIONS[i][0], LED_POSITIONS[i][1], 0);
  }
}

void ledSetAnimationEnabled(bool on) { g_animEnabled = on; }
void ledSetOverall(uint8_t v)        { g_overall = v; }
void ledSetContrast(uint8_t v)       { g_contrast = (v > 64) ? 64 : v; }
void ledSetPeriodMs(uint16_t v)      {
  if (v < 1000)  v = 1000;
  if (v > 20000) v = 20000;
  g_periodMs = v;
}
uint8_t ledGetOverall() { return g_overall; }

// Ramp the overall brightness one LED_DIM_STEP in `dir` (-1 dimmer, +1 brighter).
// Returns the new overall so the caller can detect the "clamped to 0" case.
uint8_t ledDimRampStep(int8_t dir) {
  int16_t v = int16_t(g_overall) + int16_t(dir) * LED_DIM_STEP;
  if (v < 0)   v = 0;
  if (v > 255) v = 255;
  g_overall = uint8_t(v);
  return g_overall;
}

// Bring-up: light each populated position 0..LED_COUNT-1 in order, ~1 s each.
// Blocks for ~14 s — intended for first-power-on (Serial command `w`), not loop.
void ledWalk() {
  if (!g_ledReady) {
    Serial.println("[LED] walk: not ready");
    return;
  }
  Serial.println("[LED] walk: 0..13, 1s each");
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    ledAllOff();
    g_is31.drawPixel(LED_POSITIONS[i][0], LED_POSITIONS[i][1], 200);
    Serial.print("[LED] walk i=");
    Serial.println(i);
    delay(1000);  // blocking on purpose — bring-up routine only
  }
  ledAllOff();
}
