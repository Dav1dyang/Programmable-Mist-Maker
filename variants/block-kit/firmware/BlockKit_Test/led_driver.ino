// IS31FL3731 driver — unified continuous-modulation renderer.
//
// All 14 LEDs are computed from ONE formula, every frame:
//
//   bright[i] = center
//             + breathAmp * sin(2π · t / BREATH_PERIOD)
//             + waveAmp   * sin(2π · t / WAVE_PERIOD + 2π · (LED_COUNT-1-i) / LED_COUNT)
//
// `center`, `breathAmp`, `waveAmp` are themselves interpolated continuously
// from two state-driven smoothed inputs:
//
//   base8 (0..255)        — envelope amplitude (also drives mist duty in main loop)
//   waveAct8 (0..255)     — 0 = pure idle breath, 255 = pure traveling wave
//
//   center    = lerp(IDLE_CENTER,  RUNNING_CENTER, waveAct) · base/255
//   breathAmp = IDLE_BREATH_AMP  · (255-waveAct)/255  · base/255
//   waveAmp   = RUNNING_WAVE_AMP · waveAct/255         · base/255
//
// Consequences:
//   * No "mode" — every state renders the same formula with different
//     smoothed inputs. Crossfades are continuous; nothing snaps.
//   * Both sines run on FREE-RUNNING phase derived from millis(); phase
//     never resets at a state change, so the visible motion stays alive
//     through transitions.
//   * Wave wavelength = LED_COUNT (one peak fits the strip), so peaks
//     drift bottom→top and wrap into the bottom with no spatial seam.
//   * Per-LED 8-bit PWM out, gamma-corrected, and pushed through a 14-byte
//     I2C write cache so identical frames cost zero bus traffic.

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
static uint32_t  g_ledLastRenderMs   = 0;

// Runtime-tunable so the bench-test serial commands can play with periods
// without rebuilding firmware.
static uint16_t  g_breathPeriodMs    = LED_BREATH_PERIOD_MS;
static uint16_t  g_wavePeriodMs      = LED_WAVE_PERIOD_MS;

// Per-LED I2C write cache. setLEDPWM() unconditionally bursts on the bus,
// so remembering the last byte written to each LED and skipping no-op
// writes saves the full 14-byte burst on every steady-state frame (typical
// when the envelope sits at peak with the breath/wave at a calm slope).
static uint8_t   g_lastPwm[LED_COUNT];
static bool      g_lastPwmInit       = false;

// Interpolated sine in -127..+127 from a fractional phase in [0, 64*256).
// Linear interpolation between adjacent LUT entries removes the stair-step
// artifact you'd otherwise see at the slow ~4-6 s periods we use.
static inline int16_t sineQ(uint32_t phaseQ8) {
  const uint8_t idx  = uint8_t((phaseQ8 >> 8) & 63u);
  const uint8_t next = uint8_t((idx + 1) & 63u);
  const uint8_t frac = uint8_t(phaseQ8 & 0xFFu);
  const int16_t s0 = SINE_LUT[idx];
  const int16_t s1 = SINE_LUT[next];
  return s0 + (((s1 - s0) * int16_t(frac)) >> 8);
}

void ledInit() {
  // Wire.begin() is called once in the main sketch setup.
  g_ledReady = g_is31.begin(LED_IS31_ADDR);
  if (!g_ledReady) {
    Serial.println("[LED] IS31FL3731 not found");
    return;
  }
  // Zero all 14 positions explicitly and initialize the per-LED cache.
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    g_is31.setLEDPWM(LED_MAP[i], 0, LED_IS31_FRAME);
    g_lastPwm[i] = 0;
  }
  g_lastPwmInit = true;
  Serial.println("[LED] init ok");
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
  uint8_t zero[LED_COUNT] = {0};
  writePerLed(zero);
}

// Main render. base8 (smoothed envelope amplitude) and waveAct8 (smoothed
// idle↔running interpolation knob) are both 0..255. The state machine in
// BlockKit_Test.ino owns the smoothing; this function is purely visual.
void ledRender(uint8_t base8, uint8_t waveAct8) {
  if (!g_ledReady) return;
  const uint32_t now = millis();
  if (now - g_ledLastRenderMs < LED_TICK_MS) return;
  g_ledLastRenderMs = now;

  // Fast path: zero envelope → strip dark. Skips ~ a hundred multiplies.
  if (base8 == 0) {
    uint8_t zero[LED_COUNT] = {0};
    writePerLed(zero);
    return;
  }

  // ---- Derive envelope parameters from (base8, waveAct8). Pre-multiply
  // into 16-bit so each per-LED iteration only does adds + 1 sine lookup.
  // All three of these scale linearly with base8 so the whole envelope
  // dims uniformly — the WAVE doesn't decelerate as it dims, it just
  // fades (the user explicitly asked for "soft lighting" not "snappy
  // chase"; speed scaling produced the snappy-decel feel previously).
  const uint16_t lerp1 = uint16_t(waveAct8);
  const uint16_t lerp0 = uint16_t(255u - waveAct8);

  // center8 = ((IDLE * lerp0) + (RUNNING * lerp1)) / 255    →  0..255
  // then ·base8/255. Combine the two divides as a single >>16 with the
  // standard "n*257/65536 ≈ n/255" trick to avoid two divisions per frame.
  const uint16_t centerEnv = (uint16_t(LED_IDLE_CENTER_MAX)    * lerp0
                            + uint16_t(LED_RUNNING_CENTER_MAX) * lerp1) / 255u;
  const uint16_t center    = (centerEnv * uint16_t(base8)) / 255u;

  const uint16_t breathEnv = (uint16_t(LED_IDLE_BREATH_AMP_MAX) * lerp0) / 255u;
  const uint16_t breathAmp = (breathEnv * uint16_t(base8)) / 255u;

  const uint16_t waveEnv   = (uint16_t(LED_RUNNING_WAVE_AMP_MAX) * lerp1) / 255u;
  const uint16_t waveAmp   = (waveEnv * uint16_t(base8)) / 255u;

  // ---- Free-running phases. Reduce `now` mod period BEFORE the multiply
  // so the intermediate stays bounded (otherwise it overflows uint32_t).
  const uint32_t tb = uint32_t(now) % g_breathPeriodMs;
  const uint32_t breathPhaseQ8 = (tb * 64u * 256u) / g_breathPeriodMs;
  const int16_t  breathSine = sineQ(breathPhaseQ8);   // -127..+127

  const uint32_t tw = uint32_t(now) % g_wavePeriodMs;
  const uint32_t waveTemporalQ8 = (tw * 64u * 256u) / g_wavePeriodMs;
  // Per-LED spatial offset in Q8 phase units. (LED_COUNT-1-i) so the peak
  // drifts top-ward as time advances (the canonical "rising" direction):
  // at any moment, the LED whose spatial offset cancels temporalPhase is
  // at sine peak — that's the brightest LED, and it moves upward with t.
  // 64*256 / LED_COUNT = 1170.3, integer-truncated, summed = 14·1170=16380
  // (≈ 16384) so the residual error is sub-2/16384 per cycle.
  constexpr uint32_t SPATIAL_STEP_Q8 = (64u * 256u + LED_COUNT/2) / LED_COUNT;

  uint8_t pwm[LED_COUNT];
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    const uint32_t spatial = uint32_t(LED_COUNT - 1 - i) * SPATIAL_STEP_Q8;
    const uint32_t phase   = (waveTemporalQ8 + spatial) & 0x3FFFu;  // mod 64*256
    const int16_t  waveSine = sineQ(phase);    // -127..+127

    // Combine: center + breathAmp · breathSine/127 + waveAmp · waveSine/127
    int32_t bright = int32_t(center)
                   + (int32_t(breathAmp) * breathSine) / 127
                   + (int32_t(waveAmp)   * waveSine)   / 127;
    if (bright < 0)   bright = 0;
    if (bright > 255) bright = 255;
    pwm[i] = GAMMA_LUT[bright];
  }
  writePerLed(pwm);
}

// ---- Runtime tuning hooks (called from serial command parser) -----------
void ledSetBreathPeriodMs(uint16_t v) {
  if (v < 1000)  v = 1000;
  if (v > 20000) v = 20000;
  g_breathPeriodMs = v;
}
void ledSetWavePeriodMs(uint16_t v) {
  if (v < 1000)  v = 1000;
  if (v > 20000) v = 20000;
  g_wavePeriodMs = v;
}

// Bring-up: light each LED in sequence at the same fixed brightness so the
// physical (top→bottom) order can be verified by eye. Blocking by design.
// Keeps the per-LED cache in sync with the direct writes so the final
// writePerLed(zero) actually re-clears the strip.
void ledWalk() {
  if (!g_ledReady) {
    Serial.println("[LED] walk: not ready");
    return;
  }
  Serial.println("[LED] walk: 0..13, 1s each");
  uint8_t zero[LED_COUNT] = {0};
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    writePerLed(zero);
    g_is31.setLEDPWM(LED_MAP[i], 200, LED_IS31_FRAME);
    g_lastPwm[i] = 200;
    Serial.print("[LED] walk i=");
    Serial.print(i);
    Serial.print(" lednum=");
    Serial.println(LED_MAP[i]);
    delay(1000);
  }
  writePerLed(zero);
}
