// Runtime configuration backed by ESP32 NVS (Preferences).
//
// Every UX tunable that used to be a `constexpr` in pins.h now lives here as a
// field on the global `cfg` struct. Defaults come from pins.h's CFG_DEFAULT_*
// constants. configInit() loads defaults, then overlays any NVS-saved values.
// Web UI POST /api/config calls configApplyJson() → configSave() to persist.
//
// Versioning: the saved blob carries a CONFIG_VERSION byte at the head so
// future schema changes can fall back to defaults cleanly. Bump the version
// when you add/remove/reorder fields in the saved portion (the secrets are
// stored as separate NVS keys, so they're not affected by the version).

#pragma once
#include <Arduino.h>
#include "pins.h"

constexpr uint8_t  CONFIG_VERSION    = 1;
constexpr uint16_t CFG_SHA256_HEX_LEN = 64;   // sha256 hex digest length
constexpr uint16_t CFG_OTA_PWD_MAX    = 32;
constexpr uint16_t CFG_HOSTNAME_MAX   = 32;

struct Config {
  // --- LED — BREATH (idle) ---
  uint8_t  ledBreathPeak;
  uint16_t ledBreathPeriodMs;
  uint8_t  ledBreathLow;
  // --- LED — WAVE (docked) ---
  uint8_t  waveBaseLevel;        // dim "low"
  uint8_t  waveSwellPeak;        // base + peak = full at crest
  uint16_t wavePeriodMs;
  uint16_t ledCrossfadeMs;
  // --- LED — hide/show fade (short-press toggle) ---
  uint8_t  ledScaleStepPerTick;  // 0..255 step per smoother tick
  // --- Mist ---
  uint8_t  mistDutyMax;          // "high"
  uint8_t  mistDutyMin;          // "low" floor when level > 0
  uint8_t  levelDefault;         // boot intensity
  uint16_t mistWaveTroughQ8;     // wave-sync trough (Q8: 0..256)
  // --- Level smoother ---
  uint16_t levelSmoothTickMs;
  uint8_t  levelSmoothStepUp;
  uint8_t  levelSmoothStepDn;
  uint8_t  levelSmoothStepUpFast;
  uint16_t levelRampTickMs;
  uint8_t  levelRampStep;
  // --- Button + reed ---
  uint16_t buttonDebounceMs;
  uint16_t buttonLongPressMs;
  uint16_t buttonLongTickMs;
  uint16_t reedInsertDwellMs;
  uint16_t reedRemoveDwellMs;
  // --- Status LED (D7) ---
  uint8_t  statusLedDimDuty;

  // --- Identity + secrets (separate NVS keys; NEVER returned by /api/config) ---
  char     hostname[CFG_HOSTNAME_MAX + 1];           // mDNS + ArduinoOTA hostname
  char     adminPasswordHash[CFG_SHA256_HEX_LEN + 1]; // sha256 hex of admin pwd
  char     otaPassword[CFG_OTA_PWD_MAX + 1];          // for ArduinoOTA.setPassword
};

extern Config cfg;

// Persisted byte-layout for the tunables blob in NVS. Lives in the header so
// the Arduino IDE's auto-generated forward declarations in config.ino can see
// it — putting the struct definition in the .ino broke compilation because
// the IDE inserts prototypes BEFORE the struct, and the prototypes referenced
// CfgBlob. Plain POD, no padding surprises because every field is byte/u16-
// aligned and the secrets (C-strings) live in separate NVS keys.
struct CfgBlob {
  uint8_t  version;
  uint8_t  ledBreathPeak;
  uint16_t ledBreathPeriodMs;
  uint8_t  ledBreathLow;
  uint8_t  waveBaseLevel;
  uint8_t  waveSwellPeak;
  uint16_t wavePeriodMs;
  uint16_t ledCrossfadeMs;
  uint8_t  ledScaleStepPerTick;
  uint8_t  mistDutyMax;
  uint8_t  mistDutyMin;
  uint8_t  levelDefault;
  uint16_t mistWaveTroughQ8;
  uint16_t levelSmoothTickMs;
  uint8_t  levelSmoothStepUp;
  uint8_t  levelSmoothStepDn;
  uint8_t  levelSmoothStepUpFast;
  uint16_t levelRampTickMs;
  uint8_t  levelRampStep;
  uint16_t buttonDebounceMs;
  uint16_t buttonLongPressMs;
  uint16_t buttonLongTickMs;
  uint16_t reedInsertDwellMs;
  uint16_t reedRemoveDwellMs;
  uint8_t  statusLedDimDuty;
} __attribute__((packed));

// Load defaults, then overlay NVS if a valid blob exists.
void configInit();
// Persist the current `cfg` to NVS. Returns false on NVS error.
bool configSave();
// Apply firmware defaults to `cfg` (does NOT touch NVS until configSave()).
void configResetDefaults();
// Apply a single field by name (used by /api/config). Returns false if name
// is unknown or value is out of range. On success the caller should configSave().
bool configSetField(const char* name, long value);
// Render the tunable portion of `cfg` as JSON (writes into `out`, up to `cap`
// bytes; returns bytes written, 0 on truncation). Excludes secrets.
size_t configToJson(char* out, size_t cap);
// Set admin password (stores SHA-256 hex hash). pwd MUST be NUL-terminated.
bool configSetAdminPassword(const char* pwd);
// Returns true if the supplied plain-text password hashes to the stored hash.
bool configCheckAdminPassword(const char* pwd);
// Set OTA password. Empty string disables OTA password.
bool configSetOtaPassword(const char* pwd);
// Set the network hostname (mDNS + ArduinoOTA).
bool configSetHostname(const char* name);

// Convenience: SHA-256 → 64-char lowercase hex.
void configSha256Hex(const char* in, size_t inLen, char outHex[CFG_SHA256_HEX_LEN + 1]);
