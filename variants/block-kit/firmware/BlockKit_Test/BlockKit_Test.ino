// Block Kit V0.1 — bring-up firmware (Phase A).
//
// Phase A scope: mist control + reed switch (magnetic-on UX) + button override
// + dual-mode LED effects (breath / vertical chase) + D7 dim indicator +
// scope-mode current logging. The water-level classifier (Phase B) is
// intentionally NOT in this build.
//
// State machine — four states, all centralized here:
//
//   IDLE_LEDS_OFF           — muted: LEDs dark. Reached only via short-press.
//   IDLE_LEDS_ON            — DEFAULT idle: container undocked, LED strip
//                             doing a soft uniform breath at user level.
//                             Boot lands here.
//   RUNNING                 — container docked, mist active. LED strip fades
//                             up in BREATH mode, then switches to SWIRL
//                             (rising vertical chase) at peak brightness.
//   TRANSITION_FROM_RUNNING — container just lifted: mist hard-stopped,
//                             swirl decelerates + dims to 0, then auto-enters
//                             IDLE_LEDS_ON with a fast breath fade-up. The
//                             full cinematic is ~1 s. Re-docking during the
//                             transition cleanly re-enters RUNNING.
//
// One `g_userLevel` variable (0..255) drives both mist PWM duty and LED
// brightness scale. Mist duty = (level * MIST_DUTY_MAX) / 255 so level=255
// means 50% duty (full mist). `g_targetLevel` is what each state wants the
// level to be; `smoothLevel()` ramps `g_currentLevel` toward target so every
// transition fades luxuriously instead of snapping.
//
// Container lift is a SAFETY event: mist hard-stops the moment the reed
// opens (mistHardStop), while the LED smoother continues the visual fade.
// Everything else uses the smooth path.
//
// The LED ring is a 14-LED vertical strip (top=LED 1, bottom=LED 14). All
// strip animation logic lives in led_driver.ino; this file just flips the
// mode via ledSetMode() at the right state transitions.

#include <Wire.h>
#include "pins.h"

// ---- Forward declarations of helpers defined in sibling .ino files ----
void mistInit(); bool mistIsRunning(); bool mistIsInhibited();
void mistApply(uint8_t); void mistHardStop(); void mistEnable(bool);
void containerInit(); bool containerIsPresent(); bool containerRawPresent();
ContainerEvent containerPoll();
void buttonInit();    ButtonEvent buttonPoll();
void ledInit(); void ledRender(uint8_t); void ledAllOff(); void ledWalk();
void ledSetBreathEnabled(bool); void ledSetBreathPeriodMs(uint16_t);
void ledSetBreathDepth(uint8_t);
void ledSetMode(LedMode); void ledSetSwirlFading(bool);
void statusLedInit(); void statusLedSet(bool);
void currentSenseInit(); void currentSenseTick();
void currentSenseLogPlot(uint8_t); void currentSenseToggleScope();
void currentSenseTogglePlotMute();
float currentMeanMa(); float currentVarMa2();

// ---- App-level state ----
static AppState g_state           = AppState::IDLE_LEDS_ON;  // boot default = soft breath
static uint8_t  g_userLevel       = LEVEL_DEFAULT;   // user's set level (long-press adjusts)
static uint8_t  g_targetLevel     = 0;               // state-driven target for the smoother
static uint8_t  g_currentLevel    = 0;               // smoothed actual level applied to mist+LEDs
static int8_t   g_dimDir          = -1;              // -1 = next long-press dims, +1 = brightens
static uint32_t g_lastSmoothMs    = 0;
static uint32_t g_lastRampMs      = 0;
static uint32_t g_lastStatMs      = 0;
// Pending swirl-engage flag: when entering RUNNING we hold the LED in BREATH
// mode and let the smoother fade up to target. Once it lands, the smoother
// flips to SWIRL — that's how "smoothly brighten up to max, AND THEN start
// swirling" reads as sequential rather than simultaneous.
static bool     g_pendingSwirl    = false;
// One-shot flag: use the fast STEP_UP for the next 0→target ramp. Set when
// entering IDLE_LEDS_ON from TRANSITION_FROM_RUNNING so the breath restore is
// snappy (~425 ms) instead of luxurious (~850 ms). Cleared automatically when
// the smoother reaches target.
static bool     g_fastFadeUp      = false;

static void enterIdleLedsOff();
static void enterIdleLedsOn();
static void enterRunning();
static void enterTransitionFromRunning();

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
  g_targetLevel = 0;
  g_pendingSwirl = false;
  g_fastFadeUp = false;
  ledSetMode(LedMode::BREATH);             // strip dark via baseLevel=0; mode irrelevant
  ledSetSwirlFading(false);
  if (leavingMist) mistEnable(false);      // hard-stop + inhibit
  Serial.println("[APP] -> IDLE_LEDS_OFF");
}

static void enterIdleLedsOn() {
  // Special wiring: when called from TRANSITION_FROM_RUNNING, the breath
  // restore should be faster than the normal luxurious 850 ms fade — capture
  // that BEFORE the idempotency check so the flag flips even if we somehow
  // call it twice in the same frame.
  const bool fromTransition = (g_state == AppState::TRANSITION_FROM_RUNNING);
  if (g_state == AppState::IDLE_LEDS_ON) return;
  const bool leavingMist = mistMayBeActive(g_state);
  g_state = AppState::IDLE_LEDS_ON;
  g_targetLevel = g_userLevel;
  g_pendingSwirl = false;
  g_fastFadeUp = fromTransition;
  ledSetMode(LedMode::BREATH);
  ledSetSwirlFading(false);
  if (leavingMist) mistEnable(false);
  Serial.println("[APP] -> IDLE_LEDS_ON");
}

static void enterRunning() {
  if (g_state == AppState::RUNNING) return;
  g_state = AppState::RUNNING;
  g_targetLevel = g_userLevel;
  // Stay in BREATH while we fade up — the smoother flips us to SWIRL once
  // g_currentLevel reaches g_targetLevel. This makes "brighten up to max
  // AND THEN start swirling" sequential rather than simultaneous.
  ledSetMode(LedMode::BREATH);
  ledSetSwirlFading(false);
  g_pendingSwirl = true;
  g_fastFadeUp = false;
  mistEnable(true);                        // re-arm the mist path
  Serial.println("[APP] -> RUNNING");
}

static void enterTransitionFromRunning() {
  if (g_state == AppState::TRANSITION_FROM_RUNNING) return;
  g_state = AppState::TRANSITION_FROM_RUNNING;
  g_targetLevel = 0;
  // Mode stays whatever it was (SWIRL if we hit peak, BREATH if removal
  // happened mid-ramp). Swirl decelerates with baseLevel during the fade.
  ledSetSwirlFading(true);
  g_pendingSwirl = false;
  g_fastFadeUp = false;
  mistEnable(false);                       // hard-stop + inhibit
  Serial.println("[APP] -> TRANSITION_FROM_RUNNING");
}

// ----------------------------------------------------------------------
// Level smoother — runs every LEVEL_SMOOTH_TICK_MS, advances g_currentLevel
// toward g_targetLevel. Tuned for ~850 ms 0→255 ramp normally; ~425 ms when
// g_fastFadeUp is set (one-shot, used to make the post-removal breath
// restore feel snappy).
//
// Two state-machine side effects fire here, both when the smoother lands
// on target:
//   * If state == RUNNING and g_pendingSwirl, flip LED mode to SWIRL. This
//     is what makes "fade up, THEN start swirling" sequential.
//   * If state == TRANSITION_FROM_RUNNING and target == 0, the swirl-fade
//     just finished; auto-enter IDLE_LEDS_ON so the breath fades back in.
// ----------------------------------------------------------------------
static void smoothLevel() {
  const uint32_t now = millis();
  if (now - g_lastSmoothMs < LEVEL_SMOOTH_TICK_MS) return;
  g_lastSmoothMs = now;

  if (g_currentLevel < g_targetLevel) {
    const uint8_t step = g_fastFadeUp ? LEVEL_SMOOTH_STEP_UP_FAST : LEVEL_SMOOTH_STEP_UP;
    const uint8_t room = g_targetLevel - g_currentLevel;
    g_currentLevel += (room < step) ? room : step;
  } else if (g_currentLevel > g_targetLevel) {
    const uint8_t room = g_currentLevel - g_targetLevel;
    g_currentLevel -= (room < LEVEL_SMOOTH_STEP_DN) ? room : LEVEL_SMOOTH_STEP_DN;
  }

  if (g_currentLevel != g_targetLevel) return;

  // Landed on target. Resolve any pending state-machine side effects.
  g_fastFadeUp = false;
  if (g_state == AppState::RUNNING && g_pendingSwirl) {
    ledSetMode(LedMode::SWIRL);
    g_pendingSwirl = false;
    Serial.println("[APP] swirl engaged");
  } else if (g_state == AppState::TRANSITION_FROM_RUNNING && g_targetLevel == 0) {
    enterIdleLedsOn();                     // fast breath fade-up
  }
}

// ----------------------------------------------------------------------
// Long-press level ramp: while held, adjust g_userLevel (and the target if
// we're in a state that follows user level). Direction inverts on release.
// ----------------------------------------------------------------------
static void rampUserLevel() {
  const uint32_t now = millis();
  if (now - g_lastRampMs < LEVEL_RAMP_TICK_MS) return;
  g_lastRampMs = now;

  int16_t v = int16_t(g_userLevel) + int16_t(g_dimDir) * int16_t(LEVEL_RAMP_STEP);
  if (v < 0)   v = 0;
  if (v > 255) v = 255;
  g_userLevel = uint8_t(v);

  // RUNNING and IDLE_LEDS_ON both follow the user level live. IDLE_LEDS_OFF
  // doesn't react to long-press, so we wouldn't be here in that state.
  g_targetLevel = g_userLevel;

  // Dim-to-zero snap: drop into IDLE_LEDS_OFF and reset g_userLevel to the
  // default so a subsequent short-press into RUNNING / IDLE_LEDS_ON wakes at
  // a visible level (otherwise it would come up at 0 and look broken).
  // We set g_dimDir = +1 here so the LongPressEnd flip on release lands at
  // -1, which is the correct direction for the natural next gesture: "I'm
  // back at full level, long-press to dim again."
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
  Serial.println(F("  vN            - set user level (mist + LED level), 0..255"));
  Serial.println(F("  a0 / a1       - LED breathing off / on"));
  Serial.println(F("  dN            - LED breath depth (0..64), default 16"));
  Serial.println(F("  pN            - LED breath period_ms (1000..20000)"));
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
      // Mirror the short-press button: mute if active, wake if muted. From a
      // muted state, dock state decides whether we wake into RUNNING or just
      // the ambient breath.
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
      // Apply live ONLY in the steady-state active states. IDLE_LEDS_OFF is
      // muted; TRANSITION_FROM_RUNNING is fading to 0 and overriding target
      // would interrupt the cinematic.
      const bool applyNow =
          g_state == AppState::RUNNING || g_state == AppState::IDLE_LEDS_ON;
      if (applyNow) {
        g_targetLevel = g_userLevel;
        Serial.print("[APP] user level=");
        Serial.println(v);
      } else {
        Serial.print("[APP] user level=");
        Serial.print(v);
        Serial.println(" (stored; applies on next wake)");
      }
      return;
    }
    case 'a': {
      const long v = parseTail(cmd, len);
      if (v == 0)      { ledSetBreathEnabled(false); Serial.println("[LED] breath off"); }
      else if (v == 1) { ledSetBreathEnabled(true);  Serial.println("[LED] breath on");  }
      else Serial.println("[CMD] use a0 or a1");
      return;
    }
    case 'd': {
      const long v = parseTail(cmd, len);
      if (v >= 0 && v <= 64) { ledSetBreathDepth(uint8_t(v)); Serial.print("[LED] depth="); Serial.println(v); }
      else Serial.println("[CMD] d: 0..64");
      return;
    }
    case 'p': {
      const long v = parseTail(cmd, len);
      if (v >= 1000 && v <= 20000) { ledSetBreathPeriodMs(uint16_t(v)); Serial.print("[LED] period_ms="); Serial.println(v); }
      else Serial.println("[CMD] p: 1000..20000");
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
// Event handlers — react to reed and button events. State transitions
// happen here, never inside the leaf modules.
// ----------------------------------------------------------------------
static void onContainerEvent(ContainerEvent ev) {
  if (ev == ContainerEvent::Inserted) {
    // Docking always wins — from any state, including TRANSITION_FROM_RUNNING
    // mid-fade. enterRunning() is idempotent if we're already RUNNING.
    enterRunning();
  } else if (ev == ContainerEvent::Removed) {
    // Only RUNNING triggers the cinematic. From IDLE states, removal is a
    // no-op (container wasn't there in the model). From TRANSITION_FROM_
    // RUNNING, we're already on our way out — let the smoother finish.
    if (g_state == AppState::RUNNING) {
      enterTransitionFromRunning();
    }
  }
}

static void onButtonEvent(ButtonEvent ev) {
  switch (ev) {
    case ButtonEvent::ShortPress:
      // Short-press is "mute toggle / wake from mute". It cuts straight to
      // IDLE_LEDS_OFF from any active state — including TRANSITION_FROM_
      // RUNNING (the user explicitly asked to skip the cinematic).
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
  Serial.print("[STAT] state=");       Serial.print(stateName(g_state));
  Serial.print(" reed=");              Serial.print(containerRawPresent() ? 1 : 0);
  Serial.print(" btn=");               Serial.print(digitalRead(PIN_BUTTON));
  Serial.print(" user=");              Serial.print(g_userLevel);
  Serial.print(" cur=");               Serial.print(g_currentLevel);
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

  // Print resolved GPIOs for sanity-check against the schematic. Catches
  // "wrong board selected" mistakes from the log alone.
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

  // Boot lands in IDLE_LEDS_ON (default soft breath). If a container is
  // already docked at power-on, the first containerPoll() will edge-trigger
  // Inserted after the 500 ms safety dwell and we'll head into RUNNING.
  //
  // The static initializer above already sets g_state = IDLE_LEDS_ON so the
  // idempotent enterIdleLedsOn() is a no-op; we set target + LED mode here
  // explicitly so neither depends on the leaf modules' init order, and we
  // print the resolved state to make the boot log unambiguous.
  g_targetLevel = g_userLevel;
  ledSetMode(LedMode::BREATH);
  Serial.println("[APP] state=IDLE_LEDS_ON (boot)");
  printHelp();
}

void loop() {
  pollSerial();

  // Subsystem ticks
  currentSenseTick();
  smoothLevel();

  // Apply smoothed level to outputs
  mistApply(g_currentLevel);
  ledRender(g_currentLevel);

  // Input edges + diagnostics
  onContainerEvent(containerPoll());
  onButtonEvent(buttonPoll());

  // D7 reflects container presence directly — dim when waiting for a dock,
  // off when something is docked. Independent of mist on/off and LED ring
  // state so the indicator stays a stable "is the device looking for input?"
  // signal.
  statusLedSet(!containerIsPresent());

  currentSenseLogPlot(uint8_t(g_state));
  statTick();
}
