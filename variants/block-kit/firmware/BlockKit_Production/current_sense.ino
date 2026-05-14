// INA180A3 current sense on D2.
//
// PHASE A: this file only acquires data — 1 kHz ADC into a 256-sample sliding
// window, with rolling mean and variance available cheaply on demand. Scope
// mode (`s` toggle) streams raw + mean + variance as CSV for Serial Plotter so
// David can bench-validate the water/dry separation BEFORE we add a state
// classifier (Phase B).

#include "pins.h"

static uint16_t g_window[CURRENT_WINDOW_N] = {0};
static uint16_t g_widx       = 0;
static uint32_t g_sum        = 0;    // sum of raw ADC counts in window
static uint64_t g_sumSq      = 0;    // sum of squares
static uint16_t g_filled     = 0;    // count of samples seen, capped at N

static uint32_t g_lastSampleUs   = 0;
static bool     g_scopeMode      = false;
static bool     g_plotMuted      = false;
static uint32_t g_lastScopeUs    = 0;
static uint32_t g_lastPlotMs     = 0;

void currentSenseInit() {
  // Match the bench-validated test sketch exactly: pinMode(INPUT) and rely on
  // arduino-esp32 v3.x defaults (12-bit resolution, ~3.3 V full-scale). Calling
  // analogReadResolution()/analogSetPinAttenuation() on ESP32-C6 v3.x has been
  // observed to leave the ADC stuck returning 0 — the bench sketch omits them
  // and works, so we follow the same recipe here.
  pinMode(PIN_CURRENT_ADC, INPUT);

  // Pre-seed the window with one read so the first mean is sensible.
  const uint16_t s = analogRead(PIN_CURRENT_ADC);
  for (uint16_t i = 0; i < CURRENT_WINDOW_N; ++i) g_window[i] = s;
  g_sum = uint32_t(s) * CURRENT_WINDOW_N;
  g_sumSq = uint64_t(s) * uint64_t(s) * CURRENT_WINDOW_N;
  g_filled = CURRENT_WINDOW_N;
}

// Convert a raw ADC count (0..4095, 0..3.3 V) to milliamps via INA180A3.
static inline float adcToMa(uint16_t raw) {
  const float volts = (float(raw) * 3.3f) / 4095.0f;
  return (volts * 1000.0f) / CURRENT_SENSE_FACTOR;
}

// Rolling mean of the window, in mA.
float currentMeanMa() {
  if (g_filled == 0) return 0.0f;
  const float meanRaw = float(g_sum) / float(g_filled);
  return adcToMa(uint16_t(meanRaw));
}

// Rolling variance of the window, in mA^2. Useful only as a relative number
// while we figure out the threshold — print, don't rely on yet.
float currentVarMa2() {
  if (g_filled < 2) return 0.0f;
  const float n = float(g_filled);
  const float mean = float(g_sum) / n;
  const float meanSq = float(g_sumSq) / n;
  const float varRaw = meanSq - mean * mean;
  // Convert variance from raw ADC counts^2 to mA^2.
  const float countToMa = (3.3f * 1000.0f) / (4095.0f * CURRENT_SENSE_FACTOR);
  return varRaw * countToMa * countToMa;
}

// Sample the ADC at ~1 kHz and slide the window. Call from the main loop.
void currentSenseTick() {
  const uint32_t now = micros();
  if (now - g_lastSampleUs < (1000000u / CURRENT_SAMPLE_HZ)) return;
  g_lastSampleUs = now;

  const uint16_t s = analogRead(PIN_CURRENT_ADC);

  // Subtract the slot we're about to overwrite, add the new sample.
  const uint16_t old = g_window[g_widx];
  g_sum   = g_sum   - old + s;
  g_sumSq = g_sumSq - uint64_t(old) * uint64_t(old) + uint64_t(s) * uint64_t(s);
  g_window[g_widx] = s;
  g_widx = (g_widx + 1) % CURRENT_WINDOW_N;
  if (g_filled < CURRENT_WINDOW_N) ++g_filled;

  // Scope mode: stream raw + mean + var at SCOPE_PRINT_HZ for Serial Plotter.
  if (g_scopeMode && (now - g_lastScopeUs >= (1000000u / SCOPE_PRINT_HZ))) {
    g_lastScopeUs = now;
    Serial.print("[PLOT] ");
    Serial.print(adcToMa(s), 1);
    Serial.print(',');
    Serial.print(currentMeanMa(), 1);
    Serial.print(',');
    Serial.println(currentVarMa2(), 1);
  }
}

// Periodic plotter-friendly CSV emit when scope mode is OFF.
// `state` is the AppState as int so the plotter shows a state trace too.
void currentSenseLogPlot(uint8_t stateInt) {
  if (g_scopeMode) return;  // scope mode owns the [PLOT] stream
  if (g_plotMuted)  return; // user has muted the periodic plot (Serial cmd `m`)
  const uint32_t now = millis();
  if (now - g_lastPlotMs < (1000u / PLOT_PRINT_HZ)) return;
  g_lastPlotMs = now;
  Serial.print("[PLOT] ");
  Serial.print(currentMeanMa(), 1);
  Serial.print(',');
  Serial.print(currentVarMa2(), 1);
  Serial.print(',');
  Serial.println(stateInt);
}

void currentSenseToggleScope() {
  g_scopeMode = !g_scopeMode;
  Serial.print("[CUR] scope mode ");
  Serial.println(g_scopeMode ? "ON" : "OFF");
}

void currentSenseTogglePlotMute() {
  g_plotMuted = !g_plotMuted;
  Serial.print("[CUR] plot mute ");
  Serial.println(g_plotMuted ? "ON (no [PLOT] until you send `m` again)" : "OFF");
}
