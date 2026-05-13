// Block Kit V0.1 — bring-up firmware (Phase A).
//
// Phase A scope: mist control + reed switch (magnetic-on UX) + button
// override + unified continuous-modulation LED envelope + D7 indicator +
// scope-mode current logging. The water-level classifier (Phase B) is
// intentionally NOT in this build.
//
// State machine — four states, all centralized here:
//
//   IDLE_LEDS_OFF           — muted: strip dark. Reached only via short-press.
//   IDLE_LEDS_ON            — DEFAULT idle: undocked, soft "very dim &
//                             dramatic" breath at user level. Boot lands here.
//   RUNNING                 — container docked, mist active. A 3-second
//                             single crossfade rises the center brightness
//                             while fading the breath out and the slow
//                             traveling wave in (no "mode" switch — see
//                             below).
//   TRANSITION_FROM_RUNNING — container just lifted: mist hard-stopped,
//                             envelope dims to 0 (~640 ms) preserving the
//                             wave shape, then auto-enters IDLE_LEDS_ON with
//                             a fast (~425 ms) breath restore. Total cinematic
//                             ≈ 1 s.
//
// The LED ring is a 14-LED vertical strip (top = index 0, bottom = index 13).
// There is NO LED mode. led_driver.ino renders one continuous formula every
// frame; the visible look at any moment is a smooth interpolation of two
// state-driven smoothed inputs:
//
//   g_baseLevel16        — overall envelope amplitude (also drives mist duty).
//                          Target normally tracks g_userLevel; goes to 0 in
//                          IDLE_LEDS_OFF and TRANSITION_FROM_RUNNING.
//   g_waveActivation16   — 0 = pure breath (idle look); 65535 = pure wave
//                          (running look). 3-second smooth ramp on dock/
//                          undock produces the "single crossfade" UX.
//
// Both are 16-bit so per-tick increments are sub-8-bit — accumulated
// fractional bits keep the visible output advancing smoothly across frames.
//
// Container lift is a SAFETY event: mist hard-stops the moment the reed
// opens (mistHardStop), while the LED smoother continues the visual fade.
// Everything else uses the smooth path.

#include <Wire.h>
#include "pins.h"

// ---- Forward declarations of helpers defined in sibling .ino files ----
void mistInit(); bool mistIsRunning(); bool mistIsInhibited();
void mistApply(uint8_t); void mistHardStop(); void mistEnable(bool);
void containerInit(); bool containerIsPresent(); bool containerRawPresent();
ContainerEvent containerPoll();
void buttonInit();    ButtonEvent buttonPoll();
void ledInit(); void ledRender(uint8_t, uint8_t); void ledAllOff(); void ledWalk();
void ledSetBreathPeriodMs(uint16_t); void ledSetWavePeriodMs(uint16_t);
void statusLedInit(); void statusLedSet(bool);
void currentSenseInit(); void currentSenseTick();
void currentSenseLogPlot(uint8_t); void currentSenseToggleScope();
void currentSenseTogglePlotMute();
float currentMeanMa(); float currentVarMa2();

// ---- App-level state ----
static AppState g_state                       = AppState::IDLE_LEDS_ON;
static uint8_t  g_userLevel                   = LEVEL_DEFAULT;   // long-press adjusts
static int8_t   g_dimDir                      = -1;              // -1 = next long-press dims
static uint32_t g_lastSmoothMs                = 0;
static uint32_t g_lastRampMs                  = 0;
static uint32_t g_lastStatMs                  = 0;

// 16-bit smoothed envelope inputs. >>8 gives the 8-bit values fed to mistApply
// (g_baseLevel16 only) and ledRender (both).
static uint16_t g_baseLevel16                 = 0;
static uint16_t g_baseLevelTarget16           = 0;
static uint16_t g_baseStep16                  = LEVEL_BASE_STEP_UP_16;  // dynamic per transition

static uint16_t g_waveActivation16            = 0;
static uint16_t g_waveActivationTarget16      = 0;

static void enterIdleLedsOff();
static void enterIdleLedsOn();
static void enterRunning();
static void enterTransitionFromRunning();

// Convert g_userLevel (0..255) to the 16-bit target. Multiplying by 257
// (= 65535/255) maps the full 8-bit range to the full 16-bit range without
// a divide, so g_userLevel=255 lands exactly at 0xFFFF and >>8 returns 255.
static inline uint16_t userLevel16() { return uint16_t(g_userLevel) * 257u; }

// ----------------------------------------------------------------------
// State transitions
// Idempotent — calling enterX() when already in state X is a no-op so spam
// from serial commands or repeated reed edges doesn't churn log lines.
// Leaving any state where mist may be active (RUNNING / TRANSITION_FROM_
// RUNNING) locks the mist (mistEnable(false)) so the LED smoother's non-
// zero level can't sneak the boost rail back on; entering RUNNING unlocks
// it. D7 is driven by the main loop from containerIsPresent(), NOT from
// state, so the indicator follows the magnet not the mist.
// ----------------------------------------------------------------------
static bool mistMayBeActive(AppState s) {
  return s == AppState::RUNNING || s == AppState::TRANSITION_FROM_RUNNING;
}

static void enterIdleLedsOff() {
  if (g_state == AppState::IDLE_LEDS_OFF) return;
  const bool leavingMist = mistMayBeActive(g_state);
  g_state = AppState::IDLE_LEDS_OFF;
  g_baseLevelTarget16        = 0;
  g_waveActivationTarget16   = 0;
  g_baseStep16               = LEVEL_BASE_STEP_UP_16;
  if (leavingMist) mistEnable(false);
  Serial.println("[APP] -> IDLE_LEDS_OFF");
}

static void enterIdleLedsOn() {
  // Special wiring: when called from TRANSITION_FROM_RUNNING the breath
  // restore should be faster than the normal luxurious 850 ms fade.
  const bool fromTransition = (g_state == AppState::TRANSITION_FROM_RUNNING);
  if (g_state == AppState::IDLE_LEDS_ON) return;
  const bool leavingMist = mistMayBeActive(g_state);
  g_state = AppState::IDLE_LEDS_ON;
  g_baseLevelTarget16      = userLevel16();
  g_baseStep16             = fromTransition ? LEVEL_BASE_STEP_UP_FAST_16
                                             : LEVEL_BASE_STEP_UP_16;
  // Snap waveActivation to 0 immediately. Safe because at this point either
  //   (a) we're auto-promoting from TRANSITION_FROM_RUNNING and base is 0
  //       (the strip is dark, so the shape doesn't matter), or
  //   (b) we came from IDLE_LEDS_OFF where wave was already 0.
  g_waveActivation16       = 0;
  g_waveActivationTarget16 = 0;
  if (leavingMist) mistEnable(false);
  Serial.println("[APP] -> IDLE_LEDS_ON");
}

static void enterRunning() {
  if (g_state == AppState::RUNNING) return;
  g_state = AppState::RUNNING;
  g_baseLevelTarget16      = userLevel16();
  g_baseStep16             = LEVEL_BASE_STEP_UP_16;
  g_waveActivationTarget16 = 0xFFFFu;   // pure wave look at target
  mistEnable(true);                      // re-arm the mist path
  Serial.println("[APP] -> RUNNING");
}

static void enterTransitionFromRunning() {
  if (g_state == AppState::TRANSITION_FROM_RUNNING) return;
  g_state = AppState::TRANSITION_FROM_RUNNING;
  g_baseLevelTarget16 = 0;
  // Wave activation is left at whatever value it had — we want the wave
  // SHAPE preserved during the fade-down, not crossfaded back to breath.
  mistEnable(false);                     // hard-stop + inhibit
  Serial.println("[APP] -> TRANSITION_FROM_RUNNING");
}

// ----------------------------------------------------------------------
// Smoother — runs every LEVEL_SMOOTH_TICK_MS, advances g_baseLevel16 and
// g_waveActivation16 toward their targets in 16-bit space so the per-tick
// increment can be sub-8-bit (fractional bits accumulate between ticks,
// giving the eye a perfectly continuous slide).
//
// Side effect: when state == TRANSITION_FROM_RUNNING and the base lands on
// 0, auto-enter IDLE_LEDS_ON. This is the auto-promotion that produces the
// dim-down → breath-restore cinematic.
// ----------------------------------------------------------------------
template<typename T>
static inline void smoothToward(T& current, T target, T step) {
  if (current < target) {
    const T room = target - current;
    current += (room < step) ? room : step;
  } else if (current > target) {
    const T room = current - target;
    current -= (room < step) ? room : step;
  }
}

static void smoothLevel() {
  const uint32_t now = millis();
  if (now - g_lastSmoothMs < LEVEL_SMOOTH_TICK_MS) return;
  g_lastSmoothMs = now;

  // Base envelope: per-transition step rate (set by enterX()).
  const uint16_t baseStep = (g_baseLevel16 < g_baseLevelTarget16)
      ? g_baseStep16
      : LEVEL_BASE_STEP_DN_16;
  smoothToward<uint16_t>(g_baseLevel16, g_baseLevelTarget16, baseStep);

  // Wave activation: fixed 3 s ramp rate for the dock/undock crossfade.
  smoothToward<uint16_t>(g_waveActivation16, g_waveActivationTarget16,
                         LEVEL_WAVE_ACT_STEP_16);

  // Auto-promotion: end of the removal cinematic.
  if (g_state == AppState::TRANSITION_FROM_RUNNING && g_baseLevel16 == 0) {
    enterIdleLedsOn();
  }
}

// ----------------------------------------------------------------------
// Long-press level ramp: while held, adjust g_userLevel (and the base
// target if we're in a state that follows user level). Direction inverts
// on release. Wave activation is NOT affected by long-press — the user
// asked for "brightness only" dimming with constant wave motion.
// ----------------------------------------------------------------------
static void rampUserLevel() {
  const uint32_t now = millis();
  if (now - g_lastRampMs < LEVEL_RAMP_TICK_MS) return;
  g_lastRampMs = now;

  int16_t v = int16_t(g_userLevel) + int16_t(g_dimDir) * int16_t(LEVEL_RAMP_STEP);
  if (v < 0)   v = 0;
  if (v > 255) v = 255;
  g_userLevel = uint8_t(v);

  // RUNNING and IDLE_LEDS_ON both follow the user level live.
  g_baseLevelTarget16 = userLevel16();

  // Dim-to-zero snap: drop into IDLE_LEDS_OFF and reset g_userLevel to the
  // default so a subsequent short-press into RUNNING / IDLE_LEDS_ON wakes
  // at a visible level. g_dimDir = +1 so the LongPressEnd flip on release
  // lands at -1 (the correct direction for the next gesture).
  if (g_dimDir < 0 && g_userLevel <= LEVEL_OFF_THRESHOLD) {
    g_userLevel = LEVEL_DEFAULT;
    g_dimDir = +1;
    enterIdleLedsOff();
  }
}

// ----------------------------------------------------------------------
// Serial command parser
// ----------------------------------------------------------------------
static void printHelp() {
  Serial.println(F("[CMD] commands:"));
  Serial.println(F("  help          - print this list"));
  Serial.println(F("  1 / 0 / t     - mist on / off / toggle (requires container)"));
  Serial.println(F("  vN            - set user level (mist + LED envelope), 0..255"));
  Serial.println(F("  pN            - LED breath period_ms (1000..20000), default 6500"));
  Serial.println(F("  qN            - LED wave   period_ms (1000..20000), default 4500"));
  Serial.println(F("  w             - run ledWalk (~14 s, blocks)"));
  Serial.println(F("  k             - recalibrate baseline (Phase B)"));
  Serial.println(F("  s             - toggle current-sense scope mode"));
  Serial.println(F("  r             - dump reed state (raw + debounced)"));
  Serial.println(F("  m             - mute / unmute the [PLOT] stream"));
}

static long parseTail(const char* cmd, uint8_t len) {
  if (len < 2) return -1;
  long v = 0;
  for (uint8_t i = 1; i < len; ++i) {
    const char d = cmd[i];
    if (d < '0' || d > '9') return -1;
    v = v * 10 + (d - '0');
  }
  return v;
}

static void handleCommand(const char* cmd, uint8_t len) {
  if (len == 0) return;
  if (len == 4 && cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'l' && cmd[3] == 'p') {
    printHelp();
    return;
  }

  const char c = cmd[0];
  switch (c) {
    case '1':
      if (containerIsPresent()) enterRunning();
      else Serial.println("[CMD] container not present — use button to toggle LEDs");
      return;
    case '0': enterIdleLedsOff(); return;
    case 't':
      if (g_state == AppState::IDLE_LEDS_OFF) {
        if (containerIsPresent()) enterRunning();
        else                      enterIdleLedsOn();
      } else {
        enterIdleLedsOff();
      }
      return;
    case 'v': {
      const long v = parseTail(cmd, len);
      if (v < 0 || v > 255) { Serial.println("[CMD] v: 0..255"); return; }
      g_userLevel = uint8_t(v);
      const bool applyNow =
          g_state == AppState::RUNNING || g_state == AppState::IDLE_LEDS_ON;
      if (applyNow) {
        g_baseLevelTarget16 = userLevel16();
        Serial.print("[APP] user level=");
        Serial.println(v);
      } else {
        Serial.print("[APP] user level=");
        Serial.print(v);
        Serial.println(" (stored; applies on next wake)");
      }
      return;
    }
    case 'p': {
      const long v = parseTail(cmd, len);
      if (v >= 1000 && v <= 20000) {
        ledSetBreathPeriodMs(uint16_t(v));
        Serial.print("[LED] breath period_ms=");
        Serial.println(v);
      } else Serial.println("[CMD] p: 1000..20000");
      return;
    }
    case 'q': {
      const long v = parseTail(cmd, len);
      if (v >= 1000 && v <= 20000) {
        ledSetWavePeriodMs(uint16_t(v));
        Serial.print("[LED] wave period_ms=");
        Serial.println(v);
      } else Serial.println("[CMD] q: 1000..20000");
      return;
    }
    case 'w': ledWalk(); return;
    case 'k': Serial.println("[CUR] recalibrate is a Phase B feature"); return;
    case 's': currentSenseToggleScope(); return;
    case 'r': {
      Serial.print("[REED] raw=");
      Serial.print(containerRawPresent() ? "1(LOW/magnet)" : "0(HIGH/open)");
      Serial.print(" debounced=");
      Serial.println(containerIsPresent() ? "1(present)" : "0(absent)");
      return;
    }
    case 'm': currentSenseTogglePlotMute(); return;
    default:
      Serial.print("[CMD] unknown: ");
      Serial.write(cmd, len);
      Serial.println();
  }
}

static void pollSerial() {
  static char     buf[33];
  static uint8_t  bufLen = 0;
  while (Serial.available()) {
    const char c = char(Serial.read());
    if (c == '\n' || c == '\r') {
      if (bufLen > 0) {
        buf[bufLen] = '\0';
        handleCommand(buf, bufLen);
      }
      bufLen = 0;
    } else if (bufLen < 32 && c != ' ' && c != '\t') {
      buf[bufLen++] = c;
    }
  }
}

// ----------------------------------------------------------------------
// Event handlers
// ----------------------------------------------------------------------
static void onContainerEvent(ContainerEvent ev) {
  if (ev == ContainerEvent::Inserted) {
    enterRunning();
  } else if (ev == ContainerEvent::Removed) {
    if (g_state == AppState::RUNNING) {
      enterTransitionFromRunning();
    }
  }
}

static void onButtonEvent(ButtonEvent ev) {
  switch (ev) {
    case ButtonEvent::ShortPress:
      if (g_state == AppState::IDLE_LEDS_OFF) {
        if (containerIsPresent()) enterRunning();
        else                      enterIdleLedsOn();
      } else {
        enterIdleLedsOff();
      }
      return;
    case ButtonEvent::LongPressStart:
      if (g_state == AppState::RUNNING || g_state == AppState::IDLE_LEDS_ON) {
        g_lastRampMs = millis();
        Serial.print("[BTN] long-press start, dir=");
        Serial.println(int(g_dimDir));
      }
      return;
    case ButtonEvent::LongPressTick:
      if (g_state == AppState::RUNNING || g_state == AppState::IDLE_LEDS_ON) {
        rampUserLevel();
      }
      return;
    case ButtonEvent::LongPressEnd:
      g_dimDir = -g_dimDir;
      Serial.print("[BTN] long-press end, next dir=");
      Serial.println(int(g_dimDir));
      return;
    default: return;
  }
}

// ----------------------------------------------------------------------
// [STAT] line — once per second, human-readable subsystem snapshot
// ----------------------------------------------------------------------
static const char* stateName(AppState s) {
  switch (s) {
    case AppState::IDLE_LEDS_OFF:           return "IDLE_OFF";
    case AppState::IDLE_LEDS_ON:            return "IDLE_ON";
    case AppState::RUNNING:                 return "RUNNING";
    case AppState::TRANSITION_FROM_RUNNING: return "XFADE_OUT";
  }
  return "?";
}

static void statTick() {
  const uint32_t now = millis();
  if (now - g_lastStatMs < 1000) return;
  g_lastStatMs = now;
  const uint8_t base8 = uint8_t(g_baseLevel16 >> 8);
  const uint8_t wave8 = uint8_t(g_waveActivation16 >> 8);
  Serial.print("[STAT] state=");       Serial.print(stateName(g_state));
  Serial.print(" reed=");              Serial.print(containerRawPresent() ? 1 : 0);
  Serial.print(" btn=");               Serial.print(digitalRead(PIN_BUTTON));
  Serial.print(" user=");              Serial.print(g_userLevel);
  Serial.print(" base=");              Serial.print(base8);
  Serial.print(" wave=");              Serial.print(wave8);
  Serial.print(" mist=");              Serial.print(mistIsRunning() ? 1 : 0);
  Serial.print(" mean_mA=");           Serial.println(currentMeanMa(), 1);
}

// ----------------------------------------------------------------------
// Arduino entry points
// ----------------------------------------------------------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);  // USB-CDC enumeration on XIAO ESP32-C6 takes hundreds of ms;
                // shorter delays drop the first banner/help lines silently.
  Serial.println();
  Serial.println("[APP] Block Kit V0.1 bring-up (Phase A)");

  Serial.print("[APP] PIN_MIST_PWM=D0 (GPIO ");    Serial.print(PIN_MIST_PWM);    Serial.println(")");
  Serial.print("[APP] PIN_BOOST_EN=D3 (GPIO ");    Serial.print(PIN_BOOST_EN);    Serial.println(")");
  Serial.print("[APP] PIN_CURRENT_ADC=D2 (GPIO "); Serial.print(PIN_CURRENT_ADC); Serial.println(")");
  Serial.print("[APP] PIN_BUTTON=D6 (GPIO ");      Serial.print(PIN_BUTTON);      Serial.println(")");
  Serial.print("[APP] PIN_STATUS_LED=D7 (GPIO ");  Serial.print(PIN_STATUS_LED);  Serial.println(")");
  Serial.print("[APP] PIN_REED=D10 (GPIO ");       Serial.print(PIN_REED);        Serial.println(")");

  Wire.begin();

  // Outputs / peripherals first; inputs LAST so nothing can clobber pinMode.
  mistInit();
  statusLedInit();
  currentSenseInit();
  ledInit();
  containerInit();
  buttonInit();

  // Boot lands in IDLE_LEDS_ON — the static initializer already set g_state,
  // we just resolve the targets here so the smoother starts ramping from 0
  // toward the idle envelope on the first loop tick. Container Inserted from
  // containerPoll() (after the 500 ms dwell) will then auto-promote us into
  // RUNNING if a container is already docked at power-on.
  g_baseLevelTarget16      = userLevel16();
  g_baseStep16             = LEVEL_BASE_STEP_UP_16;
  g_waveActivationTarget16 = 0;
  Serial.println("[APP] state=IDLE_LEDS_ON (boot)");
  printHelp();
}

void loop() {
  pollSerial();

  // Subsystem ticks
  currentSenseTick();
  smoothLevel();

  // Apply smoothed envelope to outputs. mistApply takes the 8-bit base;
  // ledRender takes both 8-bit base and 8-bit waveActivation.
  const uint8_t base8 = uint8_t(g_baseLevel16 >> 8);
  const uint8_t wave8 = uint8_t(g_waveActivation16 >> 8);
  mistApply(base8);
  ledRender(base8, wave8);

  // Input edges + diagnostics
  onContainerEvent(containerPoll());
  onButtonEvent(buttonPoll());

  // D7 reflects container presence directly — dim when waiting for a dock,
  // off when something is docked. Independent of state so the indicator
  // stays a stable "is the device looking for input?" signal.
  statusLedSet(!containerIsPresent());

  currentSenseLogPlot(uint8_t(g_state));
  statTick();
}
