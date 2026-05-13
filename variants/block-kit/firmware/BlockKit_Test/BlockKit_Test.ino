// Block Kit V0.1 — bring-up firmware (Phase A).
//
// Phase A scope: mist control + reed switch (magnetic-on UX) + button override
// + LED swirl + D7 breathing + scope-mode current logging. The water-level
// classifier (Phase B) is intentionally NOT in this build; we ship scope mode
// first so the variance signal can be bench-validated before any thresholds.
//
// State machine lives ONLY in this file. Leaf .ino files report events; this
// loop decides.

#include <Wire.h>
#include "pins.h"

// ---- Forward declarations of helpers defined in sibling .ino files ----
// (Enum types referenced below live in pins.h so this block doesn't need them.)
void mistInit(); void mistOn(); void mistOff(); bool mistIsRunning();
void containerInit(); bool containerIsPresent(); bool containerRawPresent();
ContainerEvent containerPoll();
void buttonInit();    ButtonEvent buttonPoll();
void ledInit(); void ledTick(); void ledAllOff(); void ledWalk();
void ledSetAnimationEnabled(bool); void ledSetMax(uint8_t); void ledSetMin(uint8_t);
void ledSetPeriodMs(uint16_t); void ledSetWavelength(uint8_t);
uint8_t ledGetMax(); uint8_t ledDimRampStep(int8_t);
void statusLedInit(); void statusLedSet(bool); void statusLedTick();
void currentSenseInit(); void currentSenseTick();
void currentSenseLogPlot(uint8_t); void currentSenseToggleScope();
void currentSenseTogglePlotMute();
float currentMeanMa(); float currentVarMa2();

// ---- App-level state ----
static AppState g_state          = AppState::IDLE;
static int8_t   g_dimDir         = -1;   // -1 = next long-press ramps dimmer, +1 = brighter
static uint32_t g_lastDimMs      = 0;
static uint32_t g_lastStatPrintMs = 0;

static void enterIdle();
static void enterRunning();

static void enterIdle() {
  if (g_state == AppState::IDLE) return;
  g_state = AppState::IDLE;
  mistOff();
  ledAllOff();
  statusLedSet(true);
  Serial.println("[APP] -> IDLE");
}

static void enterRunning() {
  if (g_state == AppState::RUNNING) return;
  g_state = AppState::RUNNING;
  statusLedSet(false);
  mistOn();
  Serial.println("[APP] -> RUNNING");
}

// ---------- Serial command parser ----------
// Single-line commands terminated by \n or \r. Format:
//   help            - print all commands
//   1 | 0 | t       - mist on / off / toggle  (override only while docked)
//   a0 | a1         - LED animation off / on
//   bN              - set overall brightness (0..255)
//   cN              - set contrast (0..64)
//   pN              - set period_ms (1000..20000)
//   w               - run ledWalk (blocking ~14 s)
//   k               - placeholder for Phase B: force recalibrate baseline
//   s               - toggle scope mode
static void printHelp() {
  Serial.println(F("[CMD] commands:"));
  Serial.println(F("  help          - print this list"));
  Serial.println(F("  1 / 0 / t     - mist on / off / toggle (requires container)"));
  Serial.println(F("  a0 / a1       - LED swirl off / on"));
  Serial.println(F("  bN            - LED max brightness (wave peak)  0..255"));
  Serial.println(F("  cN            - LED min brightness (wave floor) 0..255"));
  Serial.println(F("  pN            - LED period_ms 1000..20000"));
  Serial.println(F("  lN            - LED wavelength (LEDs/cycle) 2..64"));
  Serial.println(F("  w             - run ledWalk (~14 s, blocks)"));
  Serial.println(F("  k             - recalibrate baseline (Phase B)"));
  Serial.println(F("  s             - toggle current-sense scope mode"));
  Serial.println(F("  r             - dump reed state (raw + debounced)"));
  Serial.println(F("  m             - mute / unmute the [PLOT] stream"));
}

// Parse the numeric tail after the command letter (e.g. "b80" -> 80). Returns
// -1 if no digits follow. We accept only non-negative integers because every
// command in this firmware has a non-negative range.
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

  // Single-char + optional numeric tail.
  const char c = cmd[0];
  switch (c) {
    case '1':
      if (containerIsPresent()) enterRunning();
      else Serial.println("[CMD] container not present");
      return;
    case '0': enterIdle(); return;
    case 't':
      if (g_state == AppState::RUNNING) enterIdle();
      else if (containerIsPresent()) enterRunning();
      else Serial.println("[CMD] container not present");
      return;
    case 'a': {
      const long v = parseTail(cmd, len);
      if (v == 0)      { ledSetAnimationEnabled(false); Serial.println("[LED] anim off"); }
      else if (v == 1) { ledSetAnimationEnabled(true);  Serial.println("[LED] anim on");  }
      else Serial.println("[CMD] use a0 or a1");
      return;
    }
    case 'b': {
      const long v = parseTail(cmd, len);
      if (v >= 0 && v <= 255) { ledSetMax(uint8_t(v)); Serial.print("[LED] max="); Serial.println(v); }
      else Serial.println("[CMD] b: 0..255");
      return;
    }
    case 'c': {
      const long v = parseTail(cmd, len);
      if (v >= 0 && v <= 255) { ledSetMin(uint8_t(v)); Serial.print("[LED] min="); Serial.println(v); }
      else Serial.println("[CMD] c: 0..255");
      return;
    }
    case 'p': {
      const long v = parseTail(cmd, len);
      if (v >= 1000 && v <= 20000) { ledSetPeriodMs(uint16_t(v)); Serial.print("[LED] period_ms="); Serial.println(v); }
      else Serial.println("[CMD] p: 1000..20000");
      return;
    }
    case 'l': {
      const long v = parseTail(cmd, len);
      if (v >= 2 && v <= 64) { ledSetWavelength(uint8_t(v)); Serial.print("[LED] wavelength="); Serial.println(v); }
      else Serial.println("[CMD] l: 2..64");
      return;
    }
    case 'w': ledWalk(); return;
    case 'k': Serial.println("[CUR] recalibrate is a Phase B feature"); return;
    case 's': currentSenseToggleScope(); return;
    case 'r': {
      // Print live reed state. raw == HIGH means no magnet; LOW means magnet
      // closing the reed. Debounced is what the state machine actually uses.
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
  // Fixed-size accumulator — plan rule: no String, no dynamic allocation.
  static char     buf[33];   // 32 chars + 1 NUL guard
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

// ---------- Event handlers ----------
static void onContainerEvent(ContainerEvent ev) {
  if (ev == ContainerEvent::Inserted) {
    enterRunning();
  } else if (ev == ContainerEvent::Removed) {
    enterIdle();
  }
}

static void onButtonEvent(ButtonEvent ev) {
  switch (ev) {
    case ButtonEvent::ShortPress:
      if (g_state == AppState::RUNNING) {
        enterIdle();
      } else if (containerIsPresent()) {
        enterRunning();
      } else {
        Serial.println("[BTN] container not present, press ignored");
      }
      return;
    case ButtonEvent::LongPressStart:
      // Begin a dim ramp only when there's something to dim.
      if (g_state == AppState::RUNNING) {
        g_lastDimMs = millis();
        Serial.print("[BTN] long-press start, dir=");
        Serial.println(int(g_dimDir));
      }
      return;
    case ButtonEvent::LongPressTick: {
      if (g_state != AppState::RUNNING) return;
      const uint32_t now = millis();
      if (now - g_lastDimMs < LED_DIM_RAMP_TICK_MS) return;
      g_lastDimMs = now;
      const uint8_t v = ledDimRampStep(g_dimDir);
      if (g_dimDir < 0 && v <= LED_DIM_OFF_THRESHOLD) {
        ledSetMax(0);    // snap to off, mist stays on
        ledSetMin(0);
      }
      return;
    }
    case ButtonEvent::LongPressEnd:
      g_dimDir = -g_dimDir;
      Serial.print("[BTN] long-press end, next dir=");
      Serial.println(int(g_dimDir));
      return;
    default: return;
  }
}

// ---------- Arduino entry points ----------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);  // USB-CDC enumeration on XIAO ESP32-C6 takes hundreds of ms;
                // shorter delays drop the first banner/help lines silently.
  Serial.println();
  Serial.println("[APP] Block Kit V0.1 bring-up (Phase A)");

  // Print the resolved GPIO numbers for the input pins so the user can sanity-check
  // pin map vs schematic from the serial log alone.
  Serial.print("[APP] PIN_REED=D10 (GPIO ");   Serial.print(PIN_REED);   Serial.print(") ");
  Serial.print("PIN_BUTTON=D6 (GPIO ");        Serial.print(PIN_BUTTON); Serial.println(")");

  Wire.begin();

  // Bring up output / peripheral subsystems first.
  mistInit();
  statusLedInit();
  currentSenseInit();
  ledInit();

  // Inputs LAST so nothing else can disturb their pinMode after we set it.
  containerInit();
  buttonInit();

  // Boot in IDLE; if a container is already docked, the loop's first poll
  // will edge-trigger Inserted and bring us into RUNNING after the dwell.
  enterIdle();
  printHelp();
}

// Once a second, print a human-readable status line covering every subsystem.
// Separate from [PLOT] (which is Serial-Plotter-friendly CSV) so it doesn't
// disrupt graphing while still giving a quick text snapshot for bring-up.
static void statTick() {
  const uint32_t now = millis();
  if (now - g_lastStatPrintMs < 1000) return;
  g_lastStatPrintMs = now;
  Serial.print("[STAT] reed_raw=");    Serial.print(containerRawPresent() ? 1 : 0);
  Serial.print(" reed_deb=");          Serial.print(containerIsPresent() ? 1 : 0);
  Serial.print(" btn_raw=");           Serial.print(digitalRead(PIN_BUTTON));
  Serial.print(" state=");             Serial.print(g_state == AppState::RUNNING ? "RUN" : "IDLE");
  Serial.print(" mist=");              Serial.print(mistIsRunning() ? 1 : 0);
  Serial.print(" mean_mA=");           Serial.println(currentMeanMa(), 1);
}

void loop() {
  pollSerial();
  currentSenseTick();
  ledTick();
  statusLedTick();

  onContainerEvent(containerPoll());
  onButtonEvent(buttonPoll());

  currentSenseLogPlot(uint8_t(g_state));
  statTick();
}
