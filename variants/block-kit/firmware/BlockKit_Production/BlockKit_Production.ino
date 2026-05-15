// Block Kit V0.1 — production firmware (Phase A).
//
// Top-level state machine + main loop. Three container-driven states drive
// the mist and LED ring; an orthogonal g_ledsHidden flag (short-press toggle)
// fades the LED strip without touching mist.
//
//   IDLE                    — undocked: soft exp(sin) breath on all 14 LEDs.
//   RUNNING                 — docked: gaussian swell wave + wave-modulated mist.
//   TRANSITION_FROM_RUNNING — just lifted: mist hard-stops, wave dims to 0.
//
// Wave-mist sync: while RUNNING, mist drive is gated by the wave's gaussian
// sampled 1 LED above the strip (the piezo's physical position). See
// mistOutLevel() here + waveIntensityAtPiezo() in led_driver.ino.
//
// SAFETY: container lift hard-stops mist (mistEnable(false)); OTA upload
// hard-stops mist (see ota.ino); boost rail is forced LOW first in setup().

#include <Wire.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include "pins.h"
#include "config.h"

// ---- Forward declarations of helpers defined in sibling .ino files ----
void mistInit(); bool mistIsRunning(); bool mistIsInhibited();
void mistApply(uint8_t); void mistHardStop(); void mistEnable(bool);
void containerInit(); bool containerIsPresent(); bool containerRawPresent();
ContainerEvent containerPoll();
void buttonInit();    ButtonEvent buttonPoll();
void ledInit(); void ledRender(uint8_t); void ledAllOff(); void ledWalk();
void ledSetMode(LedMode);
uint8_t waveIntensityAtPiezo(uint32_t now);
void statusLedInit(); void statusLedSet(bool);
void currentSenseInit(); void currentSenseTick();
void currentSenseLogPlot(uint8_t); void currentSenseToggleScope();
void currentSenseTogglePlotMute();
float currentMeanMa(); float currentVarMa2();
// Piezo-sense classifier (see piezo_sense.ino):
PiezoState piezoState();
float      piezoLastProbeMa();
uint32_t   piezoWaterCountdownS();
void       piezoResetForNewDock();
void       piezoSensePeriodicWaterCheck();
void       piezoSensePeriodicDiscCheck();
bool       piezoAutoProbeForDisc();
float      piezoCalibrateWaterBaseline();
// New in production firmware:
void logInit();
size_t logSnapshot(char*, size_t);
void logPrintln(const char* s);
void logPrintf(const char* fmt, ...);
void wifiInit(); void wifiTick(); bool wifiIsSetupMode();
void wifiForgetAndReboot();
const char* wifiHostname();
void otaInit(); void otaHandle();
void webInit(); void webHandle();
// State accessors used by the web server module:
AppState  appCurrentState();
uint8_t   appUserLevel();
uint8_t   appUserLedLevel();
uint8_t   appCurrentLevel();
bool      appLedsHidden();
void      appToggleLedsHidden();
void      appKickLedWalk();
void      appSetLedLevel(uint8_t level);
void      appSetMistLedLinked(bool linked);

// ---- App-level state ----
static AppState g_state             = AppState::IDLE;  // boot default = soft breath
static uint8_t  g_userLevel         = CFG_DEFAULT_LEVEL_DEFAULT;  // overwritten from cfg in setup()
static uint8_t  g_targetLevel       = 0;    // state-driven target for the mist smoother
static uint8_t  g_currentLevel      = 0;    // smoothed mist drive level
// Parallel pair for the LED strip: when cfg.mistLedLinked the user-level
// setter mirrors mist→LED automatically, so legacy single-knob behaviour is
// preserved. When unlinked, the wave-slider on the web UI drives userLedLevel
// independently and the LED brightness rides its own smoother.
static uint8_t  g_userLedLevel      = CFG_DEFAULT_LEVEL_DEFAULT;
static uint8_t  g_targetLedLevel    = 0;
static uint8_t  g_currentLedLevel   = 0;
static int8_t   g_levelAdjustDir    = -1;   // -1 = next long-press dims, +1 = brightens
static uint32_t g_lastSmoothMs      = 0;
static uint32_t g_lastRampMs        = 0;
static uint32_t g_lastStatPrintMs   = 0;
// One-shot: pick the fast step for the next upward ramp (post-lift restore).
// Cleared once the smoother reaches target.
static bool     g_fastLevelUp       = false;

// LED visibility — orthogonal to AppState. Short-press toggles g_ledsHidden;
// a separate smoother fades g_ledScale 0↔255 over ~640 ms, multiplying the
// baseLevel handed to ledRender() so the strip fades cleanly instead of
// snapping. Mist is NOT affected by this flag — it follows g_currentLevel
// (wave-modulated in RUNNING) so the diffuser keeps running at the user-set
// level even while the visuals are blanked.
static bool     g_ledsHidden       = false;
static uint8_t  g_ledScale         = 255;   // smoothed: 0 (hidden) … 255 (visible)
static uint8_t  g_ledScaleTarget   = 255;   // 0 or 255, set by short-press
static uint32_t g_lastLedScaleMs   = 0;

static void enterIdle();
static void enterRunning();
static void enterTransitionFromRunning();

// ----------------------------------------------------------------------
// Factory reset via triple-tap of the physical reset button.
//
// On every boot we increment a counter in a dedicated NVS namespace. After
// the device runs cleanly for TAP_CLEAR_MS we wipe the counter. If the
// user resets TAP_THRESHOLD times in a row (each boot reaching less than
// TAP_CLEAR_MS), nvs_flash_erase() wipes the entire NVS partition —
// blockkit config blob, admin/OTA passwords, WiFi credentials, RF cal —
// and the device reboots into the captive portal as if it were factory new.
// ----------------------------------------------------------------------
static constexpr const char* TAP_NS         = "factory";
static constexpr const char* TAP_KEY        = "rc";
static constexpr uint8_t     TAP_THRESHOLD  = 3;
static constexpr uint32_t    TAP_CLEAR_MS   = 5000;
static uint32_t g_tapClearAtMs = 0;

static void appNvsWipeAndReboot() {
  Serial.println("[FACTORY] wiping NVS partition + rebooting");
  Serial.flush();
  // Safety: cut the boost rail before the (blocking) NVS erase.
  pinMode(PIN_BOOST_EN, OUTPUT);
  digitalWrite(PIN_BOOST_EN, LOW);
  nvs_flash_erase();   // wipes blockkit cfg, WiFi creds, WiFiManager state, RF cal
  nvs_flash_init();    // re-init the empty partition so next boot is clean
  delay(100);
  ESP.restart();
}

static void checkAndArmTapCounter() {
  Preferences p;
  if (!p.begin(TAP_NS, /*readOnly=*/false)) return;
  const uint8_t count = uint8_t(p.getUChar(TAP_KEY, 0) + 1);
  p.putUChar(TAP_KEY, count);
  p.end();
  Serial.printf("[FACTORY] reset-tap counter: %u/%u\n", count, TAP_THRESHOLD);
  if (count >= TAP_THRESHOLD) appNvsWipeAndReboot();  // never returns
  g_tapClearAtMs = millis() + TAP_CLEAR_MS;
}

static void clearTapCounterIfStable() {
  if (g_tapClearAtMs == 0) return;
  if (millis() < g_tapClearAtMs) return;
  Preferences p;
  if (p.begin(TAP_NS, /*readOnly=*/false)) {
    p.putUChar(TAP_KEY, 0);
    p.end();
  }
  g_tapClearAtMs = 0;
}

// Public entry — called by /api/cmd/factory-reset.
void appFactoryReset() { appNvsWipeAndReboot(); }

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

static void enterIdle() {
  // Post-transition (container just lifted) — use the SLOW smoother step
  // instead of the fast restore. The previous "brisk restore" (~640 ms)
  // produced a visible brightness flash when the breath's exp(sin) phase
  // happened to be near its inhale peak at the moment baseLevel ramped up.
  // With the slow step (~1.3 s 0→255), the breath emerges gently and the
  // 1.1 s WAVE→BREATH crossfade has time to dissolve naturally. Net
  // experience: dim down to dark, then a soft breath swells in instead
  // of snapping on. (The g_fastLevelUp lane is still used by other code
  // paths if anything else needs it.)
  if (g_state == AppState::IDLE) return;
  const bool leavingMist = mistMayBeActive(g_state);
  g_state = AppState::IDLE;
  g_targetLevel    = g_userLevel;
  g_targetLedLevel = g_userLedLevel;
  g_fastLevelUp = false;            // slow restore — see comment at top of enterIdle()
  // BREATH triggers the WAVE→BREATH crossfade in led_driver when coming
  // from RUNNING / TRANSITION. led_driver collapses no-op mode changes
  // for us if we were already BREATH (e.g. boot).
  ledSetMode(LedMode::BREATH);
  if (leavingMist) mistEnable(false);
  logPrintln("[APP] -> IDLE");
}

static void enterRunning() {
  if (g_state == AppState::RUNNING) return;
  g_state = AppState::RUNNING;
  g_targetLevel    = g_userLevel;
  g_targetLedLevel = g_userLedLevel;
  // Mode flip directly to WAVE — led_driver runs an automatic 1.1 s
  // crossfade from whatever the previous mode was (BREATH in idle, or
  // WAVE-still-fading if we re-dock mid-transition). The old design used a
  // `g_pendingSwirl` flag to delay the mode flip until after the smoother
  // landed at target; that produced a visible "fade up then snap to chase"
  // moment — the specific complaint we're fixing here.
  ledSetMode(LedMode::WAVE);
  g_fastLevelUp = false;
  mistEnable(true);                        // re-arm the mist path
  logPrintln("[APP] -> RUNNING");
}

static void enterTransitionFromRunning() {
  if (g_state == AppState::TRANSITION_FROM_RUNNING) return;
  g_state = AppState::TRANSITION_FROM_RUNNING;
  g_targetLevel    = 0;
  g_targetLedLevel = 0;
  // Mode STAYS as WAVE — the wave naturally dims to black as baseLevel
  // ramps 255→0 (it scales the whole render uniformly). When the smoother
  // lands at 0 it auto-enters IDLE which kicks off the WAVE→BREATH
  // crossfade for the restore.
  g_fastLevelUp = false;
  mistEnable(false);                       // hard-stop + inhibit
  logPrintln("[APP] -> TRANSITION_FROM_RUNNING");
}

// ----------------------------------------------------------------------
// Wave-modulated mist level — only meaningful in RUNNING (mist is inhibited
// in every other state, so the value is don't-care there).
//
// factor (Q8) = cfg.mistWaveTroughQ8 + ((256 - trough) * gauss) >> 8
// mist_level  = (g_currentLevel * factor) >> 8
//
// gauss = waveIntensityAtPiezo(now) — the wave's gaussian sampled at the
// piezo position (1 LED above the top), so the mist crest follows the
// wave crest *across the top of the strip*, not at any visible LED.
// ----------------------------------------------------------------------
static uint8_t mistOutLevel(uint32_t now) {
  if (g_currentLevel == 0) return 0;
  const uint16_t trough = cfg.mistWaveTroughQ8;
  const uint16_t gauss  = uint16_t(waveIntensityAtPiezo(now));
  const uint16_t span   = (trough > 256u) ? 0u : uint16_t(256u - trough);
  const uint16_t factor = trough + ((span * gauss) >> 8);
  return uint8_t((uint16_t(g_currentLevel) * factor) >> 8);
}

// ----------------------------------------------------------------------
// LED visibility helpers — short-press flips g_ledsHidden and retargets the
// scaler. Long-press / state changes never touch this; it's strictly user-
// driven via the button.
// ----------------------------------------------------------------------
static void setLedsHidden(bool hidden) {
  if (g_ledsHidden == hidden) return;
  g_ledsHidden = hidden;
  g_ledScaleTarget = hidden ? 0 : 255;
  Serial.print("[APP] LEDs ");
  Serial.println(hidden ? "hidden (mist continues at set level)" : "visible");
}

// External hook for the web UI's hide/show toggle.
void appToggleLedsHidden() { setLedsHidden(!g_ledsHidden); }

// ----------------------------------------------------------------------
// Generic asymmetric ramp — pure, no globals. One step up, a different step
// down (use the same value for symmetric easing). Clamps at target so we
// never overshoot.
// ----------------------------------------------------------------------
static uint8_t rampToward(uint8_t current, uint8_t target, uint8_t stepUp, uint8_t stepDown) {
  if (current < target) {
    const uint8_t room = target - current;
    return current + ((room < stepUp) ? room : stepUp);
  }
  if (current > target) {
    const uint8_t room = current - target;
    return current - ((room < stepDown) ? room : stepDown);
  }
  return current;
}

// Level smoother — runs every cfg.levelSmoothTickMs. Asymmetric: a one-shot
// fast lane (g_fastLevelUp) lets the post-lift breath restore feel brisk.
// State-machine side effect: when the smoother lands on 0 in
// TRANSITION_FROM_RUNNING, auto-enter IDLE to start the WAVE→BREATH crossfade.
static void smoothLevel() {
  const uint32_t now = millis();
  if (now - g_lastSmoothMs < cfg.levelSmoothTickMs) return;
  g_lastSmoothMs = now;

  const uint8_t stepUp = g_fastLevelUp ? cfg.levelSmoothStepUpFast : cfg.levelSmoothStepUp;
  g_currentLevel    = rampToward(g_currentLevel,    g_targetLevel,    stepUp, cfg.levelSmoothStepDn);
  g_currentLedLevel = rampToward(g_currentLedLevel, g_targetLedLevel, stepUp, cfg.levelSmoothStepDn);

  if (g_currentLevel != g_targetLevel) return;
  g_fastLevelUp = false;
  if (g_state == AppState::TRANSITION_FROM_RUNNING && g_targetLevel == 0) {
    enterIdle();
  }
}

// LED visibility smoother — symmetric fade for the short-press hide/show
// toggle. Mist is intentionally NOT gated by this scaler.
static void smoothLedScale() {
  const uint32_t now = millis();
  if (now - g_lastLedScaleMs < cfg.levelSmoothTickMs) return;
  g_lastLedScaleMs = now;
  g_ledScale = rampToward(g_ledScale, g_ledScaleTarget,
                          cfg.ledScaleStepPerTick, cfg.ledScaleStepPerTick);
}

// ----------------------------------------------------------------------
// Long-press level ramp: while held, adjust g_userLevel (and the target,
// since IDLE and RUNNING both follow user level live). Direction inverts
// on release. The ramp clamps at 0/255; there's no auto-snap-to-off
// anymore — turning the device "off" is a separate gesture (short-press
// hides the LEDs, lifting the container hard-stops the mist).
// ----------------------------------------------------------------------
static void rampLevel() {
  const uint32_t now = millis();
  if (now - g_lastRampMs < cfg.levelRampTickMs) return;
  g_lastRampMs = now;

  int16_t v = int16_t(g_userLevel) + int16_t(g_levelAdjustDir) * int16_t(cfg.levelRampStep);
  if (v < 0)   v = 0;
  if (v > 255) v = 255;
  g_userLevel = uint8_t(v);

  g_targetLevel = g_userLevel;
  // Hardware long-press always affects mist; mirror to LED only when linked.
  if (cfg.mistLedLinked) {
    g_userLedLevel   = g_userLevel;
    g_targetLedLevel = g_targetLevel;
  }
}

// ----------------------------------------------------------------------
// Serial command parser
// ----------------------------------------------------------------------
static void printHelp() {
  Serial.println(F("[CMD] commands:"));
  Serial.println(F("  help          - print this list"));
  Serial.println(F("  l / L / t     - hide LEDs / show LEDs / toggle (mist unaffected)"));
  Serial.println(F("  vN            - set user level (mist + LED level), 0..255"));
  Serial.println(F("  w             - run ledWalk (~14 s, blocks)"));
  Serial.println(F("  k             - recalibrate baseline (Phase B)"));
  Serial.println(F("  s             - toggle current-sense scope mode"));
  Serial.println(F("  r             - dump reed state (raw + debounced)"));
  Serial.println(F("  m             - mute / unmute the [PLOT] stream"));
}

static long parseNumberArg(const char* cmd, uint8_t len) {
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
    case 'l': setLedsHidden(true);  return;
    case 'L': setLedsHidden(false); return;
    case 't':
      // Mirror the short-press button: just toggle LED visibility. Mist
      // behaviour is unchanged — keeps running at g_userLevel whenever a
      // container is docked, regardless of LED visibility.
      setLedsHidden(!g_ledsHidden);
      return;
    case 'v': {
      const long v = parseNumberArg(cmd, len);
      if (v < 0 || v > 255) { Serial.println("[CMD] v: 0..255"); return; }
      g_userLevel = uint8_t(v);
      if (cfg.mistLedLinked) g_userLedLevel = g_userLevel;
      // IDLE and RUNNING both follow user level live; TRANSITION_FROM_RUNNING
      // is fading to 0 and overriding target would interrupt the cinematic.
      const bool applyNow = g_state != AppState::TRANSITION_FROM_RUNNING;
      if (applyNow) {
        g_targetLevel = g_userLevel;
        if (cfg.mistLedLinked) g_targetLedLevel = g_userLedLevel;
        Serial.print("[APP] user level=");
        Serial.println(v);
      } else {
        Serial.print("[APP] user level=");
        Serial.print(v);
        Serial.println(" (stored; applies after lift-cinematic)");
      }
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
  // In useAsReed mode the reed hardware is tolerated-but-ignored; auto-probe
  // drives dock detection instead.
  if (cfg.senseUseAsReed) return;

  if (ev == ContainerEvent::Inserted) {
    piezoResetForNewDock();   // clear stale classifier state for the fresh dock
    // Docking always wins — from any state, including TRANSITION_FROM_RUNNING
    // mid-fade. enterRunning() is idempotent if we're already RUNNING.
    enterRunning();
  } else if (ev == ContainerEvent::Removed) {
    // Only RUNNING triggers the cinematic. From IDLE states, removal is a
    // no-op (container wasn't there in the model). From TRANSITION_FROM_
    // RUNNING, we're already on our way out — let the smoother finish.
    if (g_state == AppState::RUNNING) enterTransitionFromRunning();
    piezoResetForNewDock();
  }
}

static void onButtonEvent(ButtonEvent ev) {
  switch (ev) {
    case ButtonEvent::ShortPress:
      // Short-press toggles ONLY the LED strip visibility (smoothly faded
      // by g_ledScale). Mist stays at the user's set level — if a container
      // is docked, the diffuser keeps running while the visuals go dark.
      setLedsHidden(!g_ledsHidden);
      return;
    case ButtonEvent::LongPressStart:
      // Long-press ramps the user level in IDLE / RUNNING; the cinematic
      // dim of TRANSITION_FROM_RUNNING is left alone.
      if (g_state != AppState::TRANSITION_FROM_RUNNING) {
        g_lastRampMs = millis();
        Serial.print("[BTN] long-press start, dir=");
        Serial.println(int(g_levelAdjustDir));
      }
      return;
    case ButtonEvent::LongPressTick:
      if (g_state != AppState::TRANSITION_FROM_RUNNING) {
        rampLevel();
      }
      return;
    case ButtonEvent::LongPressEnd:
      g_levelAdjustDir = -g_levelAdjustDir;
      Serial.print("[BTN] long-press end, next dir=");
      Serial.println(int(g_levelAdjustDir));
      return;
    default: return;
  }
}

// ----------------------------------------------------------------------
// [STAT] line — once per second, human-readable subsystem snapshot
// ----------------------------------------------------------------------
static const char* stateName(AppState s) {
  switch (s) {
    case AppState::IDLE:                    return "IDLE";
    case AppState::RUNNING:                 return "RUNNING";
    case AppState::TRANSITION_FROM_RUNNING: return "XFADE_OUT";
  }
  return "?";
}

static void statusTick() {
  const uint32_t now = millis();
  if (now - g_lastStatPrintMs < 1000) return;
  g_lastStatPrintMs = now;
  Serial.print("[STAT] state=");       Serial.print(stateName(g_state));
  Serial.print(" leds=");              Serial.print(g_ledsHidden ? "hidden" : "visible");
  Serial.print(" reed=");              Serial.print(containerRawPresent() ? 1 : 0);
  Serial.print(" btn=");               Serial.print(digitalRead(PIN_BUTTON));
  Serial.print(" user=");              Serial.print(g_userLevel);
  Serial.print(" cur=");               Serial.print(g_currentLevel);
  Serial.print(" mist=");              Serial.print(mistIsRunning() ? 1 : 0);
  Serial.print(" mean_mA=");           Serial.println(currentMeanMa(), 1);
}

// ----------------------------------------------------------------------
// Accessors so web_server.ino can read live state without touching the static
// module-private variables directly.
// ----------------------------------------------------------------------
AppState appCurrentState()    { return g_state; }
uint8_t  appUserLevel()       { return g_userLevel; }
uint8_t  appUserLedLevel()    { return g_userLedLevel; }
uint8_t  appCurrentLevel()    { return g_currentLevel; }
bool     appLedsHidden()      { return g_ledsHidden; }

static bool g_kickLedWalk = false;
void appKickLedWalk()         { g_kickLedWalk = true; }

// Status-LED override (-1 = auto, 0 = force off, 1 = force on). Used by the
// web UI manual override; auto restores the "reflect container presence" rule.
static int8_t g_statusLedOverride = -1;
void    appSetStatusLedOverride(int8_t v) { g_statusLedOverride = v; }
int8_t  appStatusLedOverride()            { return g_statusLedOverride; }

// Live user-level setter — mirrors the serial 'v' command. Skips applying
// during TRANSITION_FROM_RUNNING so the fade-out cinematic isn't interrupted.
// When mist+LED are linked (default), also mirrors to userLedLevel so the
// LED follows. In unlinked mode the wave slider drives the LED separately
// via appSetLedLevel().
void appSetLevel(uint8_t level) {
  g_userLevel = level;
  if (cfg.mistLedLinked) g_userLedLevel = level;
  if (g_state != AppState::TRANSITION_FROM_RUNNING) {
    g_targetLevel = g_userLevel;
    if (cfg.mistLedLinked) g_targetLedLevel = g_userLedLevel;
  }
}

// LED-only level setter — used in unlinked mode by the wave slider. In
// linked mode mirrors to userLevel so dragging either slider keeps both in
// lockstep.
void appSetLedLevel(uint8_t level) {
  g_userLedLevel = level;
  if (cfg.mistLedLinked) g_userLevel = level;
  if (g_state != AppState::TRANSITION_FROM_RUNNING) {
    g_targetLedLevel = g_userLedLevel;
    if (cfg.mistLedLinked) g_targetLevel = g_userLevel;
  }
}

// Toggle whether mist and LED levels move together. When (re)linking, snap
// the LED to match the mist so the two start aligned — otherwise the wave
// tile would visibly jump on the next slider drag.
void appSetMistLedLinked(bool linked) {
  cfg.mistLedLinked = linked;
  if (linked) {
    g_userLedLevel = g_userLevel;
    if (g_state != AppState::TRANSITION_FROM_RUNNING) g_targetLedLevel = g_userLedLevel;
  }
}

// Manual state override for testing without a magnet. Reuses the same
// entry transitions the reed event handler does. A subsequent real reed
// edge will resync to physical truth.
void appForceState(AppState s) {
  if (s == AppState::RUNNING)      enterRunning();
  else if (s == AppState::IDLE)    enterIdle();
}

// ----------------------------------------------------------------------
// Arduino entry points
// ----------------------------------------------------------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);  // USB-CDC enumeration on XIAO ESP32-C6 takes hundreds of ms;
                // shorter delays drop the first banner/help lines silently.
  Serial.println();
  Serial.println("[APP] Block Kit V0.1 Production (Phase A UX + WiFi/OTA)");

  // SAFETY FIRST: boost rail LOW before WiFi/OTA come up so nothing can
  // accidentally drive the piezo during init. mistInit() will re-apply this
  // shortly; doing it here too means the rail is LOW even on a partial init.
  pinMode(PIN_BOOST_EN, OUTPUT);
  digitalWrite(PIN_BOOST_EN, LOW);

  // Reset-tap detection runs BEFORE configInit() so a bad saved config
  // (e.g. mistDutyMax pushed past 200 → boost hiccup → WiFi unreachable)
  // can be wiped by triple-tapping the reset button.
  checkAndArmTapCounter();

  // Mirror Serial into a RAM ring buffer so the web UI's Debug tab can show
  // the recent log without USB attached.
  logInit();

  // Print resolved GPIOs for sanity-check against the schematic. Catches
  // "wrong board selected" mistakes from the log alone.
  Serial.print("[APP] PIN_MIST_PWM=D0 (GPIO ");    Serial.print(PIN_MIST_PWM);    Serial.println(")");
  Serial.print("[APP] PIN_BOOST_EN=D3 (GPIO ");    Serial.print(PIN_BOOST_EN);    Serial.println(")");
  Serial.print("[APP] PIN_CURRENT_ADC=D2 (GPIO "); Serial.print(PIN_CURRENT_ADC); Serial.println(")");
  Serial.print("[APP] PIN_BUTTON=D6 (GPIO ");      Serial.print(PIN_BUTTON);      Serial.println(")");
  Serial.print("[APP] PIN_STATUS_LED=D7 (GPIO ");  Serial.print(PIN_STATUS_LED);  Serial.println(")");
  Serial.print("[APP] PIN_REED=D10 (GPIO ");       Serial.print(PIN_REED);        Serial.println(")");

  // Load NVS-backed config (or firmware defaults on first boot).
  configInit();
  g_userLevel    = cfg.levelDefault;
  g_userLedLevel = cfg.levelDefault;   // boot linked unless config has a separate save (TBD)

  Wire.begin();

  // Outputs / peripherals first; inputs LAST so nothing can clobber pinMode.
  mistInit();
  statusLedInit();
  currentSenseInit();
  ledInit();
  containerInit();
  buttonInit();

  // WiFi onboarding via WiFiManager (blocks until STA or 3 min portal timeout).
  // During the captive portal the device stays in IDLE — the LED ring shows
  // the normal soft breath so the user can see it's alive while joining.
  wifiInit();
  otaInit();
  webInit();

  // Boot lands in IDLE (default soft breath). If a container is already
  // docked at power-on, the first containerPoll() will edge-trigger
  // Inserted after the 500 ms safety dwell and we'll head into RUNNING.
  g_targetLevel = g_userLevel;
  ledSetMode(LedMode::BREATH);
  Serial.println("[APP] state=IDLE (boot, LEDs visible)");
  printHelp();
}

void loop() {
  // Networking handled FIRST every loop so an OTA recovery push can still
  // land even if a downstream subsystem (LED driver, smoother) misbehaves.
  otaHandle();
  webHandle();
  wifiTick();

  const uint32_t now = millis();
  pollSerial();

  // Subsystem ticks
  currentSenseTick();
  smoothLevel();
  smoothLedScale();
  clearTapCounterIfStable();   // 5 s after boot, reset the triple-tap counter

  // Apply smoothed level to outputs.
  //
  // Mist: wave-modulated at the piezo position (only meaningful in RUNNING;
  // mist is inhibited everywhere else, so mistApply is a no-op there).
  // LEDs: scaled by g_ledScale (the short-press hide/show fade) so the
  // strip can be blanked without touching mist.
  mistApply(mistOutLevel(now));
  // LEDs ride their own smoothed level (g_currentLedLevel) so the wave
  // slider in unlinked mode can dim the visuals without touching the mist.
  // In linked mode appSetLevel() keeps userLedLevel in lockstep with
  // userLevel, so behaviour is identical to the pre-link era.
  const uint8_t ledBase =
      uint8_t((uint16_t(g_currentLedLevel) * uint16_t(g_ledScale)) >> 8);
  ledRender(ledBase);

  // Input edges + diagnostics
  onContainerEvent(containerPoll());
  onButtonEvent(buttonPoll());

  // Piezo classifier dispatch — RUNNING runs the water probe + fault check;
  // IDLE+useAsReed runs the auto-probe for dock detection. Mutually exclusive.
  if (g_state == AppState::RUNNING) {
    piezoSensePeriodicWaterCheck();
    if (cfg.senseUseAsReed) piezoSensePeriodicDiscCheck();   // reed events are ignored — need fast removal signal
    const PiezoState ps = piezoState();
    if (ps == PiezoState::WATER_DEPLETED || ps == PiezoState::DISC_DISCONNECTED) {
      logPrintln(ps == PiezoState::WATER_DEPLETED
                       ? "[APP] water depleted — fading out"
                       : "[APP] disc disconnected — fading out");
      enterTransitionFromRunning();
    }
  } else if (g_state == AppState::IDLE && cfg.senseUseAsReed) {
    if (piezoAutoProbeForDisc()) enterRunning();
  }

  // D7 reflects container presence directly (dim when waiting for a dock,
  // off when docked) unless a web-UI override forces a specific state.
  if (g_statusLedOverride == 0)      statusLedSet(false);
  else if (g_statusLedOverride == 1) statusLedSet(true);
  else                               statusLedSet(!containerIsPresent());

  currentSenseLogPlot(uint8_t(g_state));
  statusTick();

  // Web UI may request a one-shot LED chase via /api/cmd/walk.
  if (g_kickLedWalk) {
    g_kickLedWalk = false;
    ledWalk();
  }
}
