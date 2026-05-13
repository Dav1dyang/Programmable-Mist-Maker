// IS31FL3731 driver — uniform breathing across all 14 LEDs.
//
// Matrix-B addressing: the 14 populated LEDs sit on CB1/CB2 and are written via
// setLEDPWM(lednum, pwm, 0). drawPixel(x,y,…) writes Matrix A which is unpopulated.
//
// Render pipeline per tick:
//   1. Smoothed `baseLevel` (0..255) is passed in by the main loop.
//   2. Apply a uniform breath modulation around baseLevel using a 64-entry
//      sine LUT with linear interpolation for continuous motion.
//   3. Gamma-correct (2.2) so perceived brightness scales smoothly.
//   4. Write the same PWM value to all 14 LEDs.
//
// No swirl. The animation is identical across LEDs by design — the breath
// reads as a single column gently pulsing rather than a moving wave.

#include <Adafruit_IS31FL3731.h>
#include "pins.h"

// Signed sine LUT, range -127..+127, one cycle = 64 entries.
static const int8_t SINE_LUT[64] = {
    0,  12,  24,  37,  48,  60,  71,  81,  90,  98, 106, 112, 117, 122, 125, 126,
  127, 126, 125, 122, 117, 112, 106,  98,  90,  81,  71,  60,  48,  37,  24,  12,
    0, -12, -24, -37, -48, -60, -71, -81, -90, -98,-106,-112,-117,-122,-125,-126,
 -127,-126,-125,-122,-117,-112,-106, -98, -90, -81, -71, -60, -48, -37, -24, -12,
};

// 2.2 gamma table — same values as Adafruit NeoPixel reference.
static const uint8_t GAMMA_LUT[256] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,
    2,   3,   3,   3,   3,   3,   3,   3,   4,   4,   4,   4,   4,   5,   5,   5,
    5,   6,   6,   6,   6,   7,   7,   7,   7,   8,   8,   8,   9,   9,   9,  10,
   10,  10,  11,  11,  11,  12,  12,  13,  13,  13,  14,  14,  15,  15,  16,  16,
   17,  17,  18,  18,  19,  19,  20,  20,  21,  21,  22,  22,  23,  24,  24,  25,
   25,  26,  27,  27,  28,  29,  29,  30,  31,  31,  32,  33,  34,  34,  35,  36,
   37,  37,  38,  39,  40,  40,  41,  42,  43,  44,  45,  46,  46,  47,  48,  49,
   50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  65,
   66,  67,  68,  69,  70,  72,  73,  74,  75,  76,  77,  79,  80,  81,  82,  83,
   85,  86,  87,  89,  90,  91,  92,  94,  95,  97,  98,  99, 101, 102, 104, 105,
  106, 108, 109, 111, 112, 114, 115, 117, 119, 120, 122, 124, 125, 127, 129, 130,
  132, 134, 136, 137, 139, 141, 143, 145, 146, 148, 150, 152, 154, 156, 158, 160,
  162, 164, 167, 169, 171, 173, 175, 177, 180, 182, 184, 186, 189, 191, 193, 196,
  198, 200, 203, 205, 208, 210, 213, 215, 218, 220, 223, 225, 228, 231, 233, 236,
};

static Adafruit_IS31FL3731 g_is31;
static bool      g_ledReady          = false;
static bool      g_breathEnabled     = true;
static uint16_t  g_breathPeriodMs    = LED_BREATH_PERIOD_MS;
static uint8_t   g_breathDepth       = LED_BREATH_DEPTH;
static uint32_t  g_ledLastRenderMs   = 0;

// Interpolated sine in -127..+127 from a fractional phase in [0, 64*256).
// Linear interpolation between adjacent LUT entries removes the stair-step
// artifact you'd otherwise see at the slow ~4 s breath period.
static inline int16_t sineQ(uint32_t phaseQ8) {
  const uint8_t idx  = uint8_t((phaseQ8 >> 8) & 63u);
  const uint8_t next = uint8_t((idx + 1) & 63u);
  const uint8_t frac = uint8_t(phaseQ8 & 0xFFu);
  const int16_t s0 = SINE_LUT[idx];
  const int16_t s1 = SINE_LUT[next];
  // Equivalent to s0 + (s1 - s0) * frac/256, but in integer.
  return s0 + (((s1 - s0) * int16_t(frac)) >> 8);
}

void ledInit() {
  // Wire.begin() is called once in the main sketch setup.
  g_ledReady = g_is31.begin(LED_IS31_ADDR);
  if (!g_ledReady) {
    Serial.println("[LED] IS31FL3731 not found");
    return;
  }
  // Zero all 14 positions explicitly — stock Adafruit_IS31FL3731 has clear()
  // but going through the populated map is cheap and avoids touching the
  // unpopulated Matrix A registers.
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    g_is31.setLEDPWM(LED_MAP[i], 0, LED_IS31_FRAME);
  }
  Serial.println("[LED] init ok");
}

// Push all 14 LEDs to the same gamma-corrected PWM value.
static void writeAll(uint8_t pwm) {
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    g_is31.setLEDPWM(LED_MAP[i], pwm, LED_IS31_FRAME);
  }
}

void ledAllOff() {
  if (!g_ledReady) return;
  writeAll(0);
}

// Render one frame. `baseLevel` is the smoothed level (0..255) coming from the
// main loop. The breath modulation is added on top.
void ledRender(uint8_t baseLevel) {
  if (!g_ledReady) return;
  const uint32_t now = millis();
  if (now - g_ledLastRenderMs < LED_TICK_MS) return;
  g_ledLastRenderMs = now;

  if (baseLevel == 0) {
    writeAll(0);
    return;
  }

  int16_t bright = baseLevel;
  if (g_breathEnabled && g_breathDepth > 0) {
    // Q8 phase so the breath is smooth (no stair-stepping) even at slow periods.
    const uint32_t phaseQ8 = (uint32_t(now) * 64u * 256u) / g_breathPeriodMs;
    const int16_t s = sineQ(phaseQ8);                     // -127..+127
    // Amplitude scales with baseLevel so the breath disappears at low levels
    // instead of dragging the LEDs to 0.
    const int16_t amp = (int16_t(g_breathDepth) * int16_t(baseLevel)) >> 8;
    const int16_t delta = (s * amp) / 127;
    bright += delta;
    if (bright < 0)   bright = 0;
    if (bright > 255) bright = 255;
  }
  writeAll(GAMMA_LUT[bright]);
}

void ledSetBreathEnabled(bool on) { g_breathEnabled = on; }
void ledSetBreathPeriodMs(uint16_t v) {
  if (v < 1000)  v = 1000;
  if (v > 20000) v = 20000;
  g_breathPeriodMs = v;
}
void ledSetBreathDepth(uint8_t v) {
  if (v > 64) v = 64;
  g_breathDepth = v;
}

// Bring-up: light each LED in sequence at the same fixed brightness so the
// physical (top→bottom) order can be verified by eye. Blocking by design.
void ledWalk() {
  if (!g_ledReady) {
    Serial.println("[LED] walk: not ready");
    return;
  }
  Serial.println("[LED] walk: 0..13, 1s each");
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    writeAll(0);
    g_is31.setLEDPWM(LED_MAP[i], 200, LED_IS31_FRAME);
    Serial.print("[LED] walk i=");
    Serial.print(i);
    Serial.print(" lednum=");
    Serial.println(LED_MAP[i]);
    delay(1000);
  }
  writeAll(0);
}
