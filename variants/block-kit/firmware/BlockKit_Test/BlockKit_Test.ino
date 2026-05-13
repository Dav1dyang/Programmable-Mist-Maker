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
void containerInit(); bool containerIsPresent(); ContainerEvent containerPoll();
void buttonInit();    ButtonEvent buttonPoll();
void ledInit(); void ledTick(); void ledAllOff(); void ledWalk();
void ledSetAnimationEnabled(bool); void ledSetOverall(uint8_t);
void ledSetContrast(uint8_t); void ledSetPeriodMs(uint16_t);
uint8_t ledGetOverall(); uint8_t ledDimRampStep(int8_t);
void statusLedInit(); void statusLedSet(bool); void statusLedTick();
void currentSenseInit(); void currentSenseTick();
void currentSenseLogPlot(uint8_t); void currentSenseToggleScope();
float currentMeanMa(); float currentVarMa2();

// ---- App-level state ----
static AppState g_state    = AppState::IDLE;
static int8_t   g_dimDir   = -1;     // -1 = next long-press ramps dimmer, +1 = brighter
static uint32_t g_lastDimMs = 0;

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
  Serial.println(F("  bN            - LED overall brightness 0..255"));
  Serial.println(F("  cN            - LED contrast 0..64"));
  Serial.println(F("  pN            - LED period_ms 1000..20000"));
  Serial.println(F("  w             - run ledWalk (~14 s, blocks)"));
  Serial.println(F("  k             - recalibrate baseline (Phase B)"));
  Serial.println(F("  s             - toggle current-sense scope mode"));
}

static long parseTail(const String& cmd) {
  if (cmd.length() < 2) return -1;
  return cmd.substring(1).toInt();
}

static void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;
  if (cmd == "help") { printHelp(); return; }

  // single-char + numeric tail
  const char c = cmd.charAt(0);
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
      const long v = parseTail(cmd);
      if (v == 0)      { ledSetAnimationEnabled(false); Serial.println("[LED] anim off"); }
      else if (v == 1) { ledSetAnimationEnabled(true);  Serial.println("[LED] anim on");  }
      else Serial.println("[CMD] use a0 or a1");
      return;
    }
    case 'b': {
      const long v = parseTail(cmd);
      if (v >= 0 && v <= 255) { ledSetOverall(uint8_t(v)); Serial.print("[LED] overall="); Serial.println(v); }
      else Serial.println("[CMD] b: 0..255");
      return;
    }
    case 'c': {
      const long v = parseTail(cmd);
      if (v >= 0 && v <= 64) { ledSetContrast(uint8_t(v)); Serial.print("[LED] contrast="); Serial.println(v); }
      else Serial.println("[CMD] c: 0..64");
      return;
    }
    case 'p': {
      const long v = parseTail(cmd);
      if (v >= 1000 && v <= 20000) { ledSetPeriodMs(uint16_t(v)); Serial.print("[LED] period_ms="); Serial.println(v); }
      else Serial.println("[CMD] p: 1000..20000");
      return;
    }
    case 'w': ledWalk(); return;
    case 'k': Serial.println("[CUR] recalibrate is a Phase B feature"); return;
    case 's': currentSenseToggleScope(); return;
    default:
      Serial.print("[CMD] unknown: ");
      Serial.println(cmd);
  }
}

static void pollSerial() {
  static String buf;
  while (Serial.available()) {
    const char c = char(Serial.read());
    if (c == '\n' || c == '\r') {
      if (buf.length()) handleCommand(buf);
      buf = "";
    } else if (buf.length() < 32) {
      buf += c;
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
        ledSetOverall(0);  // snap to off, mist stays on
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
  delay(50);  // brief settle for USB-CDC; we're still in setup()
  Serial.println();
  Serial.println("[APP] Block Kit V0.1 bring-up (Phase A)");

  Wire.begin();

  mistInit();
  statusLedInit();
  containerInit();
  buttonInit();
  currentSenseInit();
  ledInit();

  // Boot in IDLE; if a container is already docked, the loop's first poll
  // will edge-trigger Inserted and bring us into RUNNING after the dwell.
  enterIdle();
  printHelp();
}

void loop() {
  pollSerial();
  currentSenseTick();
  ledTick();
  statusLedTick();

  onContainerEvent(containerPoll());
  onButtonEvent(buttonPoll());

  currentSenseLogPlot(uint8_t(g_state));
}
