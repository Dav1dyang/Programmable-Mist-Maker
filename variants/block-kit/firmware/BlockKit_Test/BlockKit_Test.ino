// Block Kit V0.1 — bring-up firmware (Phase A).
//
// Phase A scope: mist control + reed switch (magnetic-on UX) + button override
// + uniform LED breathing + D7 dim indicator + scope-mode current logging. The
// water-level classifier (Phase B) is intentionally NOT in this build.
//
// State machine — three top-level states, all centralized here:
//
//   IDLE_LEDS_OFF  — no container docked, LED ring dark, D7 dim "waiting"
//   IDLE_LEDS_ON   — no container docked, LED ring breathing at user level
//                    (ambient/showcase mode toggled by button short-press)
//   RUNNING        — container docked, mist active, LED ring breathing,
//                    D7 off (ring takes over)
//
// One `g_userLevel` variable (0..255) drives both mist PWM duty and LED ring
// brightness. Mist duty = (level * MIST_DUTY_MAX) / 255 so level=255 means
// 50% duty (full mist). `g_targetLevel` is what each state wants the level to
// be; the `smoothLevel()` step ramps `g_currentLevel` toward target over time
// so every state transition fades luxuriously instead of snapping.
//
// Container lift is a SAFETY event: mist hard-stops the moment the reed
// opens (mistHardStop), while the LED ring continues fading smoothly via the
// smoother. Everything else uses the smooth path.

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
void statusLedInit(); void statusLedSet(bool);
void currentSenseInit(); void currentSenseTick();
void currentSenseLogPlot(uint8_t); void currentSenseToggleScope();
void currentSenseTogglePlotMute();
float currentMeanMa(); float currentVarMa2();

// ---- App-level state ----
static AppState g_state           = AppState::IDLE_LEDS_OFF;
static uint8_t  g_userLevel       = LEVEL_DEFAULT;   // user's set level (long-press adjusts)
static uint8_t  g_targetLevel     = 0;               // state-driven target for the smoother
static uint8_t  g_currentLevel    = 0;               // smoothed actual level applied to mist+LEDs
static int8_t   g_dimDir          = -1;              // -1 = next long-press dims, +1 = brightens
static uint32_t g_lastSmoothMs    = 0;
static uint32_t g_lastRampMs      = 0;
static uint32_t g_lastStatMs      = 0;

static void enterIdleLedsOff();
static void enterIdleLedsOn();
static void enterRunning();

// ----------------------------------------------------------------------
// State transitions
// Idempotent — calling enterX() when already in state X is a no-op so spam
// from serial commands or repeated reed edges doesn't churn log lines.
// Leaving RUNNING locks the mist (mistEnable(false)) so the LED smoother's
// non-zero level can't sneak the boost rail back on; entering RUNNING
// unlocks it. D7 is driven by the main loop from containerIsPresent(),
// NOT from state, so the indicator follows the magnet not the mist.
// ----------------------------------------------------------------------
static void enterIdleLedsOff() {
  if (g_state == AppState::IDLE_LEDS_OFF) return;
  const bool leavingRunning = (g_state == AppState::RUNNING);
  g_state = AppState::IDLE_LEDS_OFF;
  g_targetLevel = 0;
  if (leavingRunning) mistEnable(false);   // hard-stop + inhibit
  Serial.println("[APP] -> IDLE_LEDS_OFF");
}

static void enterIdleLedsOn() {
  if (g_state == AppState::IDLE_LEDS_ON) return;
  const bool leavingRunning = (g_state == AppState::RUNNING);
  g_state = AppState::IDLE_LEDS_ON;
  g_targetLevel = g_userLevel;
  if (leavingRunning) mistEnable(false);
  Serial.println("[APP] -> IDLE_LEDS_ON");
}

static void enterRunning() {
  if (g_state == AppState::RUNNING) return;
  g_state = AppState::RUNNING;
  g_targetLevel = g_userLevel;
  mistEnable(true);                        // re-arm the mist path
  Serial.println("[APP] -> RUNNING");
}

// ----------------------------------------------------------------------
// Level smoother — runs every LEVEL_SMOOTH_TICK_MS, advances g_currentLevel
// toward g_targetLevel. Tuned for ~800 ms 0→255 ramp (luxurious feel).
// Down-step is slightly larger than up-step so fade-outs don't drag.
// ----------------------------------------------------------------------
static void smoothLevel() {
  const uint32_t now = millis();
  if (now - g_lastSmoothMs < LEVEL_SMOOTH_TICK_MS) return;
  g_lastSmoothMs = now;
  if (g_currentLevel < g_targetLevel) {
    const uint8_t room = g_targetLevel - g_currentLevel;
    g_currentLevel += (room < LEVEL_SMOOTH_STEP_UP) ? room : LEVEL_SMOOTH_STEP_UP;
  } else if (g_currentLevel > g_targetLevel) {
    const uint8_t room = g_currentLevel - g_targetLevel;
    g_currentLevel -= (room < LEVEL_SMOOTH_STEP_DN) ? room : LEVEL_SMOOTH_STEP_DN;
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

  // Dim-to-zero snap: drop into IDLE_LEDS_OFF, reset direction so the next
  // long-press starts brightening, and restore g_userLevel to the default so
  // a subsequent short-press into RUNNING / IDLE_LEDS_ON wakes at a visible
  // level (otherwise it would come up at 0 and look broken).
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
      if (g_state == AppState::RUNNING) enterIdleLedsOff();
      else if (containerIsPresent()) enterRunning();
      else Serial.println("[CMD] container not present");
      return;
    case 'v': {
      const long v = parseTail(cmd, len);
      if (v >= 0 && v <= 255) {
        g_userLevel = uint8_t(v);
        if (g_state != AppState::IDLE_LEDS_OFF) g_targetLevel = g_userLevel;
        Serial.print("[APP] user level=");
        Serial.println(v);
      } else Serial.println("[CMD] v: 0..255");
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
    enterRunning();
  } else if (ev == ContainerEvent::Removed) {
    enterIdleLedsOff();
  }
}

static void onButtonEvent(ButtonEvent ev) {
  switch (ev) {
    case ButtonEvent::ShortPress:
      if (g_state == AppState::RUNNING) {
        // Container is docked but user wants the mist off — go to IDLE_LEDS_OFF.
        enterIdleLedsOff();
      } else if (containerIsPresent()) {
        // Docked but currently IDLE → start mist.
        enterRunning();
      } else {
        // No container — toggle ambient LED breathing.
        if (g_state == AppState::IDLE_LEDS_ON) enterIdleLedsOff();
        else                                   enterIdleLedsOn();
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
    case AppState::IDLE_LEDS_OFF: return "IDLE_OFF";
    case AppState::IDLE_LEDS_ON:  return "IDLE_ON";
    case AppState::RUNNING:       return "RUNNING";
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

  // Boot in IDLE_LEDS_OFF. If a container is already docked at power-on the
  // first poll will edge-trigger Inserted after the 500 ms safety dwell.
  enterIdleLedsOff();
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
