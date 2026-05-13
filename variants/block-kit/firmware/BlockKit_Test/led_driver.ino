// IS31FL3731 driver — two render modes for the 14-LED vertical strip.
//
// Matrix-B addressing: the 14 populated LEDs sit on CB1/CB2 and are written via
// setLEDPWM(lednum, pwm, 0). drawPixel(x,y,…) writes Matrix A which is unpopulated.
//
// Two modes, switched by the state machine via ledSetMode():
//   BREATH — uniform sine-modulated breath across all 14 LEDs. Used while
//            idle (no container) and during the fade-up before swirl kicks in.
//   SWIRL  — a rising vertical chase: a bright "head" travels bottom→top with
//            a SWIRL_TAIL_LEDS-long fading tail, wrapping continuously. Used
//            while a container is docked.
//
// Per-tick pipeline (LED_TICK_MS = 20 ms → 50 fps):
//   1. Dispatch on g_ledMode.
//   2. Compute per-LED 0..255 brightness (uniform for BREATH, position-based
//      for SWIRL using a Q8 phase accumulator for sub-LED smoothness).
//   3. Scale by baseLevel (smoothed level from main loop).
//   4. Gamma-correct (2.2) so perceived brightness scales smoothly.
//   5. Write through a 14-byte per-LED cache so identical frames produce no
//      I2C traffic (typical during steady BREATH at peak).
//
// During TRANSITION_FROM_RUNNING the state machine sets ledSetSwirlFading(true);
// the swirl's phase accumulator then advances proportionally to baseLevel,
// so motion slows in lockstep with the dim — a "wind down" feel.

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

// Current render mode. State machine flips this via ledSetMode().
static LedMode   g_ledMode           = LedMode::BREATH;
// SWIRL phase accumulator in Q8 units. Modular over (LED_COUNT * 256). We
// accumulate from millis() deltas (rather than computing phase from now())
// because the speed is dynamic during TRANSITION_FROM_RUNNING — a phase
// computed from absolute time can't represent a non-constant rate.
static uint32_t  g_swirlPhaseQ8      = 0;
static uint32_t  g_swirlLastMs       = 0;
// When true, swirl phase advance scales with baseLevel — slows as it dims.
// Set by the state machine on entry to TRANSITION_FROM_RUNNING.
static bool      g_swirlFading       = false;

// Per-LED I2C write cache. setLEDPWM() unconditionally bursts on the bus, so
// remembering the last byte written to each LED and skipping no-op writes
// saves the full 14-byte burst on every steady-state frame.
static uint8_t   g_lastPwm[LED_COUNT];
static bool      g_lastPwmInit       = false;

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
  // unpopulated Matrix A registers. Initialize the per-LED cache to match.
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    g_is31.setLEDPWM(LED_MAP[i], 0, LED_IS31_FRAME);
    g_lastPwm[i] = 0;
  }
  g_lastPwmInit = true;
  Serial.println("[LED] init ok");
}

// Push the same gamma-corrected PWM value to every LED, skipping any LED
// whose cached value already matches.
static void writeAll(uint8_t pwm) {
  if (!g_lastPwmInit) return;
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    if (g_lastPwm[i] != pwm) {
      g_is31.setLEDPWM(LED_MAP[i], pwm, LED_IS31_FRAME);
      g_lastPwm[i] = pwm;
    }
  }
}

// Push a per-LED frame, skipping LEDs whose cached value already matches.
static void writePerLed(const uint8_t pwm[LED_COUNT]) {
  if (!g_lastPwmInit) return;
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    if (g_lastPwm[i] != pwm[i]) {
      g_is31.setLEDPWM(LED_MAP[i], pwm[i], LED_IS31_FRAME);
      g_lastPwm[i] = pwm[i];
    }
  }
}

void ledAllOff() {
  if (!g_ledReady) return;
  writeAll(0);
}

void ledSetMode(LedMode m) {
  if (g_ledMode == m) return;
  g_ledMode = m;
  if (m == LedMode::SWIRL) {
    // Reset phase + clock so the chase enters from the bottom regardless of
    // how long we sat in BREATH. Otherwise the head could appear mid-strip.
    g_swirlPhaseQ8 = 0;
    g_swirlLastMs = millis();
  }
}

void ledSetSwirlFading(bool fading) { g_swirlFading = fading; }

// ---- Render helpers ------------------------------------------------------

static void renderBreath(uint8_t baseLevel, uint32_t now) {
  if (baseLevel == 0) {
    writeAll(0);
    return;
  }
  int16_t bright = baseLevel;
  if (g_breathEnabled && g_breathDepth > 0) {
    // Reduce `now` mod period BEFORE the *64*256 multiply so the intermediate
    // result stays bounded. Without this, `now * 16384` overflows uint32_t
    // every ~262 s and the breath glitches at each wrap.
    const uint32_t t = uint32_t(now) % g_breathPeriodMs;
    const uint32_t phaseQ8 = (t * 64u * 256u) / g_breathPeriodMs;
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

static void renderSwirl(uint8_t baseLevel, uint32_t now) {
  // Advance the phase accumulator by (dt_ms * LED_COUNT * 256 / PERIOD_MS).
  // In fading mode, scale by baseLevel so motion slows in lockstep with dim.
  const uint32_t dt = now - g_swirlLastMs;
  g_swirlLastMs = now;
  if (dt > 0) {
    uint32_t inc = (dt * uint32_t(LED_COUNT) * 256u) / SWIRL_PERIOD_MS;
    if (g_swirlFading) inc = (inc * uint32_t(baseLevel)) >> 8;
    const uint32_t POS_MAX = uint32_t(LED_COUNT) * 256u;
    g_swirlPhaseQ8 = (g_swirlPhaseQ8 + inc) % POS_MAX;
  }

  if (baseLevel == 0) {
    writeAll(0);
    return;
  }

  // Compute each LED's brightness from its position behind the rising head.
  // The strip is indexed top→bottom (i=0 is LED 1 / top, i=13 is LED 14 /
  // bottom). The head rises (visually moves toward smaller index) by treating
  // the bottom as the head's reference origin: as g_swirlPhaseQ8 grows, the
  // head sweeps from index 13 toward index 0. The tail trails BELOW the head
  // (toward larger index), and wraps modularly so the strip loops cleanly.
  const int32_t POS_MAX_S    = int32_t(LED_COUNT) * 256;
  const int32_t TAIL_RANGE_S = int32_t(SWIRL_TAIL_LEDS) * 256;
  uint8_t pwm[LED_COUNT];
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    // tail_dist = how far "behind" the head (in Q8 LED units) this LED is.
    // Equals (i*256) minus head position, modular over POS_MAX. Head position
    // at phase=0 is (LED_COUNT-1)*256 (bottom); decreases as phase grows.
    int32_t td = (int32_t(i) - int32_t(LED_COUNT - 1)) * 256
               + int32_t(g_swirlPhaseQ8);
    td = ((td % POS_MAX_S) + POS_MAX_S) % POS_MAX_S;

    uint16_t b = 0;
    if (td < TAIL_RANGE_S) {
      // Linear fade from 255 at head (td=0) to 0 at tail end (td=TAIL_RANGE).
      b = uint16_t(255u - uint16_t((uint32_t(td) * 255u) / uint32_t(TAIL_RANGE_S)));
    }
    // Scale per-LED brightness by the smoothed level, then gamma-correct.
    const uint8_t scaled = uint8_t((b * uint16_t(baseLevel)) >> 8);
    pwm[i] = GAMMA_LUT[scaled];
  }
  writePerLed(pwm);
}

// Render one frame. Dispatched on g_ledMode; both modes share gamma + cache.
void ledRender(uint8_t baseLevel) {
  if (!g_ledReady) return;
  const uint32_t now = millis();
  if (now - g_ledLastRenderMs < LED_TICK_MS) return;
  g_ledLastRenderMs = now;

  if (g_ledMode == LedMode::SWIRL) {
    renderSwirl(baseLevel, now);
  } else {
    renderBreath(baseLevel, now);
  }
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
// Keeps the per-LED cache in sync with the direct writes so a subsequent
// writeAll(0) actually re-clears the strip (the cache must believe LED i
// is at 200 for the final writeAll(0) to send a 0 to it).
void ledWalk() {
  if (!g_ledReady) {
    Serial.println("[LED] walk: not ready");
    return;
  }
  Serial.println("[LED] walk: 0..13, 1s each");
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    writeAll(0);
    g_is31.setLEDPWM(LED_MAP[i], 200, LED_IS31_FRAME);
    g_lastPwm[i] = 200;
    Serial.print("[LED] walk i=");
    Serial.print(i);
    Serial.print(" lednum=");
    Serial.println(LED_MAP[i]);
    delay(1000);
  }
  writeAll(0);
}
