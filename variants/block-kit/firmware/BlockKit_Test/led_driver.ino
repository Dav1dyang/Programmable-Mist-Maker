// IS31FL3731 driver + gentle upward-traveling swirl.
//
// Matrix-B addressing: the 14 populated LEDs sit on CB1/CB2 and are written via
// setLEDPWM(lednum, pwm, 0) where lednum maps to the chip's PWM register space.
// drawPixel(x,y,…) goes through Adafruit_GFX and writes Matrix A by default,
// which on this PCB is unpopulated — that path looks "correct" in code but lights
// nothing on the board.
//
// Animation uses a 64-entry integer sine LUT (no sinf() — ESP32-C6 has no FPU)
// followed by a 256-entry gamma 2.2 LUT for perceptual smoothness. The wave
// travels upward: at any given moment the brighter LEDs sit higher in the column,
// and the highlight slides toward the top over `period_ms`.

#include <Adafruit_IS31FL3731.h>
#include "pins.h"

// Signed sine LUT, range -127..+127, one cycle = 64 entries.
static const int8_t SINE_LUT[64] = {
    0,  12,  24,  37,  48,  60,  71,  81,  90,  98, 106, 112, 117, 122, 125, 126,
  127, 126, 125, 122, 117, 112, 106,  98,  90,  81,  71,  60,  48,  37,  24,  12,
    0, -12, -24, -37, -48, -60, -71, -81, -90, -98,-106,-112,-117,-122,-125,-126,
 -127,-126,-125,-122,-117,-112,-106, -98, -90, -81, -71, -60, -48, -37, -24, -12,
};

// Standard 2.2 gamma table — same values as Adafruit NeoPixel reference.
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
static bool     g_ledReady       = false;
static bool     g_animEnabled    = true;
static uint8_t  g_maxBright      = LED_DEFAULT_MAX;
static uint8_t  g_minBright      = LED_DEFAULT_MIN;
static uint16_t g_periodMs       = LED_DEFAULT_PERIOD_MS;
static uint8_t  g_wavelength     = LED_DEFAULT_WAVELEN;
static uint32_t g_ledLastTickMs  = 0;

void ledInit() {
  // Wire.begin() is called once in the main sketch setup.
  g_ledReady = g_is31.begin(LED_IS31_ADDR);
  if (!g_ledReady) {
    Serial.println("[LED] IS31FL3731 not found");
    return;
  }
  g_is31.clear();
  Serial.println("[LED] init ok");
}

void ledAllOff() {
  if (!g_ledReady) return;
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    g_is31.setLEDPWM(LED_MAP[i], 0, LED_IS31_FRAME);
  }
}

// Compute one frame of the wave and push to the chip. Cheap if not due yet.
void ledTick() {
  if (!g_ledReady) return;
  const uint32_t now = millis();
  if (now - g_ledLastTickMs < LED_TICK_MS) return;
  g_ledLastTickMs = now;

  // Continuous phase in LUT entries; wraps naturally via the &63 mask.
  const uint32_t phaseEntries = (now * 64u) / g_periodMs;

  const int32_t range = int32_t(g_maxBright) - int32_t(g_minBright);

  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    uint8_t bright;
    if (g_animEnabled) {
      // Spatial offset = (i / wavelength) cycles, in LUT entries.
      const uint32_t spatial = (uint32_t(i) * 64u) / g_wavelength;
      const uint8_t idx = uint8_t((phaseEntries + spatial) & 63u);
      const int16_t s = SINE_LUT[idx];                 // -127..+127
      // lerp(min, max, (s+127)/254)
      const int32_t v = int32_t(g_minBright) + (range * (int32_t(s) + 127)) / 254;
      bright = uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v));
    } else {
      bright = g_maxBright;
    }
    g_is31.setLEDPWM(LED_MAP[i], GAMMA_LUT[bright], LED_IS31_FRAME);
  }
}

void ledSetAnimationEnabled(bool on) { g_animEnabled = on; }
void ledSetMax(uint8_t v)            { g_maxBright = v; }
void ledSetMin(uint8_t v)            { g_minBright = v; }
void ledSetPeriodMs(uint16_t v)      {
  if (v < 1000)  v = 1000;
  if (v > 20000) v = 20000;
  g_periodMs = v;
}
void ledSetWavelength(uint8_t v)     {
  if (v < 2)  v = 2;
  if (v > 64) v = 64;
  g_wavelength = v;
}
uint8_t ledGetMax() { return g_maxBright; }

// Ramp the peak brightness one LED_DIM_STEP in `dir` (-1 dimmer, +1 brighter).
// The trough scales proportionally so the contrast stays sensible at low levels.
uint8_t ledDimRampStep(int8_t dir) {
  int16_t v = int16_t(g_maxBright) + int16_t(dir) * LED_DIM_STEP;
  if (v < 0)   v = 0;
  if (v > 255) v = 255;
  g_maxBright = uint8_t(v);
  // Keep min below max by at least one step.
  if (g_minBright >= g_maxBright) {
    g_minBright = g_maxBright > 1 ? g_maxBright - 1 : 0;
  }
  return g_maxBright;
}

// Bring-up: light each populated LED 0..LED_COUNT-1 in order, ~1 s each.
// Blocking by design — only invoked from the `w` Serial command.
void ledWalk() {
  if (!g_ledReady) {
    Serial.println("[LED] walk: not ready");
    return;
  }
  Serial.println("[LED] walk: 0..13, 1s each");
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    ledAllOff();
    g_is31.setLEDPWM(LED_MAP[i], 200, LED_IS31_FRAME);
    Serial.print("[LED] walk i=");
    Serial.print(i);
    Serial.print(" lednum=");
    Serial.println(LED_MAP[i]);
    delay(1000);
  }
  ledAllOff();
}
