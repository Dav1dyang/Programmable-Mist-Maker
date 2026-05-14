// LED ring driver — IS31FL3731 Matrix B, 14-LED vertical strip.
//
// Two modes (BREATH for idle, WAVE for docked) with an automatic 1.1 s
// crossfade between them. 50 fps render pipeline: render(s) → blend →
// scale by baseLevel → gamma → per-LED write cache (skips no-op I2C bursts).

#include <Adafruit_IS31FL3731.h>
#include "pins.h"
#include "config.h"

// LUT size — 64-entry lookup tables (one inhale/exhale cycle for breath,
// 0..4σ for gauss). Size is a power of 2 so we can mask instead of mod.
constexpr uint8_t LUT_SIZE = 64;
constexpr uint8_t LUT_MASK = LUT_SIZE - 1;   // 0x3F — index wrap for breath

// Gauss LUT covers 4σ in 64 entries → each entry = σ/16 Q8 units = 64.
// So gauss index = dist_q8 / 64 = dist_q8 >> GAUSS_LUT_SHIFT.
constexpr uint8_t GAUSS_LUT_SHIFT = 6;       // log2(WAVE_SIGMA_LEDS_Q8 / 16)
constexpr uint8_t GAUSS_FRAC_LSHIFT = 8 - GAUSS_LUT_SHIFT;  // 0..63 → Q8

// Signed sine LUT — kept for any helper that wants a -127..+127 sine. Not
// used by the breath or wave renderers directly (those have dedicated LUTs).
static const int8_t SINE_LUT[LUT_SIZE] = {
    0,  12,  24,  37,  48,  60,  71,  81,  90,  98, 106, 112, 117, 122, 125, 126,
  127, 126, 125, 122, 117, 112, 106,  98,  90,  81,  71,  60,  48,  37,  24,  12,
    0, -12, -24, -37, -48, -60, -71, -81, -90, -98,-106,-112,-117,-122,-125,-126,
 -127,-126,-125,-122,-117,-112,-106, -98, -90, -81, -71, -60, -48, -37, -24, -12,
};

// exp(sin) BREATH curve LUT — one full inhale/exhale cycle.
// entry[i] = round( 255 * (exp(sin(2π·i/LUT_SIZE)) - e⁻¹) / (e − e⁻¹) ).
// Asymmetric: exhale lingers at 0 (entries ~46..50 hold full black), inhale
// ramps fast. That's what makes it read as "breathing" instead of "pulsing".
static const uint8_t BREATH_LUT[LUT_SIZE] = {
   69,  80,  92, 105, 119, 134, 149, 165, 180, 195, 209, 221, 233, 242, 249, 253,
  255, 253, 249, 242, 233, 221, 209, 195, 180, 165, 149, 134, 119, 105,  92,  80,
   69,  58,  49,  41,  34,  28,  22,  18,  14,  10,   7,   5,   3,   2,   1,   0,
    0,   0,   1,   2,   3,   5,   7,  10,  14,  18,  22,  28,  34,  41,  49,  58,
};

// Gaussian WAVE LUT — covers 0..4σ. entry[i] = round(255 * exp(-(i/16)² / 2)).
// At 1σ ≈ 61% peak, at 2σ ≈ 14%, at 3σ ≈ 1%. With σ=4 LEDs on a 14-LED strip
// the swell occupies the middle ~⅓ — soft, broad, no hard edges.
static const uint8_t GAUSS_LUT[LUT_SIZE] = {
  255, 255, 253, 251, 247, 243, 238, 232, 225, 218, 210, 201, 192, 183, 174, 164,
  155, 145, 135, 126, 117, 108,  99,  91,  83,  75,  68,  61,  55,  49,  44,  39,
   35,  30,  27,  23,  20,  18,  15,  13,  11,  10,   8,   7,   6,   5,   4,   3,
    3,   2,   2,   2,   1,   1,   1,   1,   1,   0,   0,   0,   0,   0,   0,   0,
};

// 2.2 gamma table — same values as Adafruit NeoPixel reference. Applied last,
// so all blending and scaling above happens in linear (pre-gamma) space.
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
static uint32_t  g_ledLastRenderMs   = 0;

// Render mode + crossfade. ledSetMode() captures the prior mode and starts a
// crossfade timer. While elapsed < cfg.ledCrossfadeMs we render both modes per
// tick and linearly blend. After that we collapse g_prevMode = g_ledMode and
// only the current mode renders — saving ~half the math in steady state.
static LedMode   g_ledMode           = LedMode::BREATH;
static LedMode   g_prevMode          = LedMode::BREATH;
static uint32_t  g_crossfadeStartMs  = 0;

// Per-LED I2C write cache. setLEDPWM() unconditionally bursts on the bus, so
// remembering the last byte written and skipping no-op writes is meaningful —
// the breath's exhale-at-zero is a long steady state.
static uint8_t   g_lastPwm[LED_COUNT];
static bool      g_lastPwmInit       = false;

// Interpolated lookup into BREATH_LUT. Returns 0..255. Linear interpolation
// between entries removes stair-step on the slow 5.5 s breath period.
static inline uint8_t breathValue(uint32_t phaseQ8) {
  const uint8_t idx  = uint8_t((phaseQ8 >> 8) & LUT_MASK);
  const uint8_t next = uint8_t((idx + 1)      & LUT_MASK);
  const uint8_t frac = uint8_t(phaseQ8 & 0xFFu);
  const int16_t v0 = BREATH_LUT[idx];
  const int16_t v1 = BREATH_LUT[next];
  return uint8_t(v0 + (((v1 - v0) * int16_t(frac)) >> 8));
}

// Gaussian lookup. dist_q8 is unsigned Q8 LED-units. The LUT covers 0..4σ
// in LUT_SIZE entries. See GAUSS_LUT_SHIFT comment for the index derivation.
static inline uint8_t gaussValue(uint16_t dist_q8) {
  static_assert(WAVE_SIGMA_LEDS_Q8 == 4 * 256,
                "GAUSS_LUT step assumes σ = 4 LEDs (1024 Q8); retune GAUSS_LUT_SHIFT");
  uint16_t idx = dist_q8 >> GAUSS_LUT_SHIFT;
  if (idx >= LUT_SIZE) return 0;
  const uint8_t next = (idx >= LUT_SIZE - 1) ? LUT_SIZE - 1 : uint8_t(idx + 1);
  const uint8_t frac = uint8_t(dist_q8 & ((1u << GAUSS_LUT_SHIFT) - 1)) << GAUSS_FRAC_LSHIFT;
  const int16_t g0 = GAUSS_LUT[idx];
  const int16_t g1 = GAUSS_LUT[next];
  return uint8_t(g0 + (((g1 - g0) * int16_t(frac)) >> 8));
}

void ledInit() {
  // Wire.begin() is called once in the main sketch setup.
  g_ledReady = g_is31.begin(LED_IS31_ADDR);
  if (!g_ledReady) {
    Serial.println("[LED] IS31FL3731 not found");
    return;
  }
  // Zero all 14 positions explicitly — stock Adafruit_IS31FL3731 has clear()
  // but going through the populated map avoids touching Matrix A registers.
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    g_is31.setLEDPWM(LED_MAP[i], 0, LED_IS31_FRAME);
    g_lastPwm[i] = 0;
  }
  g_lastPwmInit = true;
  Serial.println("[LED] init ok");
}

// Push a per-LED final frame to the chip, skipping any LED whose cached value
// already matches. Inputs are already gamma-corrected.
// Hardware quirk: the 14 populated LEDs are on Matrix B (CB1/CB2) — must use
// setLEDPWM(lednum, pwm, 0); drawPixel(x,y,…) writes Matrix A which is
// unpopulated on this board and would silently no-op.
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
  uint8_t z[LED_COUNT] = {0};
  writePerLed(z);
}

void ledSetMode(LedMode m) {
  if (g_ledMode == m) return;
  g_prevMode = g_ledMode;        // capture the mode we're leaving
  g_ledMode  = m;                // current mode = target
  g_crossfadeStartMs = millis(); // start the blend window
}

// ---- Render helpers ------------------------------------------------------
//
// Both renderers produce per-LED raw 0..255 brightness in *pre-gamma, pre-
// scale* space. Crossfade blending and baseLevel scaling happen in ledRender.

static void renderBreathRaw(uint32_t now, uint8_t out[LED_COUNT]) {
  // Reduce now mod period BEFORE the LUT_SIZE*256 multiply so the intermediate
  // stays bounded in uint32_t (otherwise wraps ~262 s and the curve glitches).
  const uint16_t period = cfg.ledBreathPeriodMs ? cfg.ledBreathPeriodMs : 1;
  const uint32_t t       = uint32_t(now) % period;
  const uint32_t phaseQ8 = (t * LUT_SIZE * 256u) / period;
  const uint8_t  curve   = breathValue(phaseQ8);  // 0..255 — full-range LUT
  // Map the 0..255 curve onto [ledBreathLow .. ledBreathPeak]. The peak
  // keeps idle dim regardless of baseLevel; the low value floors the
  // exhale (default 0 = full black on exhale, original behavior).
  const uint8_t lo   = cfg.ledBreathLow;
  const uint8_t hi   = cfg.ledBreathPeak;
  const uint8_t span = hi >= lo ? uint8_t(hi - lo) : 0;
  const uint8_t  pwm = uint8_t(uint16_t(lo) + ((uint16_t(curve) * span) >> 8));
  for (uint8_t i = 0; i < LED_COUNT; ++i) out[i] = pwm;
}

// Position of the wave swell center in Q8 LED units. The strip is indexed
// top→bottom (i=0 top, i=13 bottom), and the user asked for the swell to
// rise from bottom to top — so y decreases as t advances (head moves
// toward lower index = upward). The swell travels from y = LED_COUNT + 3σ
// (off-screen below) to y = -3σ (off-screen above) over cfg.wavePeriodMs,
// then wraps. The jump is invisible: at ±3σ from any visible LED the
// gaussian is < 1% peak, so the strip just sits at cfg.waveBaseLevel for a
// tick during the wrap before a new swell emerges smoothly from below.
//
// Shared between renderWaveRaw and waveIntensityAtPiezo so the visible LED
// swell and the mist modulation are guaranteed phase-locked off the same
// `now` — the mist you feel is literally the wave you see.
static inline int32_t waveCenterYQ8(uint32_t now) {
  const int32_t spanQ8 = int32_t(LED_COUNT) * 256
                        + int32_t(WAVE_TRAVEL_PAD_Q8) * 2;
  const uint16_t period = cfg.wavePeriodMs ? cfg.wavePeriodMs : 1;
  const uint32_t t = uint32_t(now) % period;
  return int32_t(LED_COUNT) * 256 + int32_t(WAVE_TRAVEL_PAD_Q8)
       - int32_t((uint64_t(spanQ8) * t) / period);
}

static void renderWaveRaw(uint32_t now, uint8_t out[LED_COUNT]) {
  const int32_t waveCenterQ8 = waveCenterYQ8(now);
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    const int32_t ledPosQ8 = int32_t(i) * 256;
    int32_t       distQ8   = ledPosQ8 - waveCenterQ8;
    if (distQ8 < 0) distQ8 = -distQ8;
    const uint8_t gauss    = (distQ8 > 0xFFFF) ? 0 : gaussValue(uint16_t(distQ8));
    // base + (peak * gauss / 255), clamped in case retuning overflows the sum.
    uint16_t v = uint16_t(cfg.waveBaseLevel)
               + ((uint16_t(cfg.waveSwellPeak) * uint16_t(gauss)) >> 8);
    if (v > 255) v = 255;
    out[i] = uint8_t(v);
  }
}

// Returns the wave gaussian intensity (0..255) at the piezo's position —
// 1 LED above index 0 (configurable via MIST_PIEZO_OFFSET_LEDS_Q8). When
// the swell is centered at the piezo, returns 255 (mist crest); when far
// away, returns 0 (mist trough). The main loop multiplies this through
// g_currentLevel + the cfg.mistWaveTroughQ8 floor to produce the actual
// mist drive level — see `mistOutLevel()` in BlockKit_Production.ino.
//
// Because the piezo is physically above the top LED, the gaussian peaks at
// the piezo *after* the LED at index 0 has already peaked — so the mist
// "continues to grow" once the wave has visibly passed the top, then dims
// back down with the wave as it exits off-screen above. That timing is
// the whole point of evaluating here instead of at index 0.
uint8_t waveIntensityAtPiezo(uint32_t now) {
  const int32_t waveCenterQ8 = waveCenterYQ8(now);
  const int32_t piezoPosQ8   = -int32_t(MIST_PIEZO_OFFSET_LEDS_Q8);  // above i=0
  int32_t       distQ8       = piezoPosQ8 - waveCenterQ8;
  if (distQ8 < 0) distQ8 = -distQ8;
  if (distQ8 > 0xFFFF) return 0;
  return gaussValue(uint16_t(distQ8));
}

static inline void renderRaw(LedMode m, uint32_t now, uint8_t out[LED_COUNT]) {
  if (m == LedMode::WAVE) renderWaveRaw(now, out);
  else                    renderBreathRaw(now, out);
}

// ---- Top-level frame -----------------------------------------------------

void ledRender(uint8_t baseLevel) {
  if (!g_ledReady) return;
  const uint32_t now = millis();
  if (now - g_ledLastRenderMs < LED_TICK_MS) return;
  g_ledLastRenderMs = now;

  // 1. Current mode's raw per-LED brightness.
  uint8_t curr[LED_COUNT];
  renderRaw(g_ledMode, now, curr);

  // 2. Optional crossfade blend with the prior mode.
  uint8_t raw[LED_COUNT];
  if (g_prevMode != g_ledMode) {
    const uint32_t elapsed = now - g_crossfadeStartMs;
    const uint16_t xfade = cfg.ledCrossfadeMs;
    if (xfade > 0 && elapsed < xfade) {
      uint8_t prev[LED_COUNT];
      renderRaw(g_prevMode, now, prev);
      // Blend factor in 0..256 — 0 = all prev, 256 = all curr. We use 256
      // (not 255) so curr*mix at full mix is curr*256 = exactly curr after
      // the >>8 — no rounding bias.
      const uint16_t mix = uint16_t((elapsed * 256u) / xfade);
      const uint16_t inv = 256u - mix;
      for (uint8_t i = 0; i < LED_COUNT; ++i) {
        raw[i] = uint8_t((uint16_t(prev[i]) * inv + uint16_t(curr[i]) * mix) >> 8);
      }
    } else {
      // Crossfade window expired — collapse so steady state skips the prev
      // render pass next frame.
      g_prevMode = g_ledMode;
      for (uint8_t i = 0; i < LED_COUNT; ++i) raw[i] = curr[i];
    }
  } else {
    for (uint8_t i = 0; i < LED_COUNT; ++i) raw[i] = curr[i];
  }

  // 3. Scale by baseLevel (smoothed level 0..255 from main loop). 4. Gamma.
  uint8_t out[LED_COUNT];
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    // raw * baseLevel / 255 ≈ (raw * baseLevel) >> 8 — close enough; the
    // last 1/255 LSB doesn't survive gamma quantization.
    const uint8_t scaled = uint8_t((uint16_t(raw[i]) * uint16_t(baseLevel)) >> 8);
    out[i] = GAMMA_LUT[scaled];
  }
  writePerLed(out);
}

// ---- Bring-up helper -----------------------------------------------------

// Light each LED in sequence at the same fixed brightness so the physical
// (top→bottom) order can be verified by eye. Blocking by design. Keeps the
// per-LED cache in sync with direct writes so the trailing zero-frame
// actually clears (cache must know LED i was at 200 to send a 0 next).
void ledWalk() {
  if (!g_ledReady) {
    Serial.println("[LED] walk: not ready");
    return;
  }
  Serial.println("[LED] walk: 0..13, 1s each");
  uint8_t z[LED_COUNT] = {0};
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    writePerLed(z);
    g_is31.setLEDPWM(LED_MAP[i], 200, LED_IS31_FRAME);
    g_lastPwm[i] = 200;
    Serial.print("[LED] walk i=");
    Serial.print(i);
    Serial.print(" lednum=");
    Serial.println(LED_MAP[i]);
    delay(1000);
  }
  writePerLed(z);
}
