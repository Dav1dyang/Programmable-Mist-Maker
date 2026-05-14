// Runtime config implementation — defaults, NVS load/save, JSON serialization,
// password hashing.

#include <Preferences.h>
#include <mbedtls/sha256.h>
#include "config.h"

// One namespace for everything we persist.
static constexpr const char* NS = "blockkit";
// One key for the binary blob containing all UX tunables.
static constexpr const char* KEY_BLOB = "cfg_v1";
// Separate keys for secrets / identity so versioning the blob doesn't lose
// the user's password if we ever bump CONFIG_VERSION.
static constexpr const char* KEY_HOST     = "host";
static constexpr const char* KEY_ADMIN_PW = "admin_pw";
static constexpr const char* KEY_OTA_PW   = "ota_pw";

Config cfg;

// CfgBlob byte-layout lives in config.h — the Arduino IDE auto-generates
// forward declarations for the helpers below ABOVE this file, so a struct
// defined here wouldn't be visible to them.

// --------------------------------------------------------------------------
// SHA-256 helper (mbedtls is bundled with arduino-esp32 core).
// --------------------------------------------------------------------------

void configSha256Hex(const char* in, size_t inLen, char outHex[CFG_SHA256_HEX_LEN + 1]) {
  uint8_t digest[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);                 // 0 = SHA-256 (not SHA-224)
  mbedtls_sha256_update(&ctx, (const unsigned char*)in, inLen);
  mbedtls_sha256_finish(&ctx, digest);
  mbedtls_sha256_free(&ctx);
  // Arduino's Print.h #defines HEX as 16 for Serial.print(value, HEX). Use a
  // distinct name here to avoid the preprocessor mangling our string literal.
  static const char* HEX_CHARS = "0123456789abcdef";
  for (int i = 0; i < 32; ++i) {
    outHex[i * 2 + 0] = HEX_CHARS[(digest[i] >> 4) & 0xF];
    outHex[i * 2 + 1] = HEX_CHARS[digest[i] & 0xF];
  }
  outHex[CFG_SHA256_HEX_LEN] = '\0';
}

// --------------------------------------------------------------------------
// Defaults
// --------------------------------------------------------------------------

void configResetDefaults() {
  cfg.ledBreathPeak           = CFG_DEFAULT_LED_BREATH_PEAK;
  cfg.ledBreathPeriodMs       = CFG_DEFAULT_LED_BREATH_PERIOD_MS;
  cfg.ledBreathLow            = CFG_DEFAULT_LED_BREATH_LOW;
  cfg.waveBaseLevel           = CFG_DEFAULT_WAVE_BASE_LEVEL;
  cfg.waveSwellPeak           = CFG_DEFAULT_WAVE_SWELL_PEAK;
  cfg.wavePeriodMs            = CFG_DEFAULT_WAVE_PERIOD_MS;
  cfg.ledCrossfadeMs          = CFG_DEFAULT_LED_CROSSFADE_MS;
  cfg.ledScaleStepPerTick     = CFG_DEFAULT_LED_SCALE_STEP_PER_TICK;
  cfg.mistDutyMax             = CFG_DEFAULT_MIST_DUTY_MAX;
  cfg.mistDutyMin             = CFG_DEFAULT_MIST_DUTY_MIN;
  cfg.levelDefault            = CFG_DEFAULT_LEVEL_DEFAULT;
  cfg.mistWaveTroughQ8        = CFG_DEFAULT_MIST_WAVE_TROUGH_Q8;
  cfg.levelSmoothTickMs       = CFG_DEFAULT_LEVEL_SMOOTH_TICK_MS;
  cfg.levelSmoothStepUp       = CFG_DEFAULT_LEVEL_SMOOTH_STEP_UP;
  cfg.levelSmoothStepDn       = CFG_DEFAULT_LEVEL_SMOOTH_STEP_DN;
  cfg.levelSmoothStepUpFast   = CFG_DEFAULT_LEVEL_SMOOTH_STEP_UP_FAST;
  cfg.levelRampTickMs         = CFG_DEFAULT_LEVEL_RAMP_TICK_MS;
  cfg.levelRampStep           = CFG_DEFAULT_LEVEL_RAMP_STEP;
  cfg.buttonDebounceMs        = CFG_DEFAULT_BUTTON_DEBOUNCE_MS;
  cfg.buttonLongPressMs       = CFG_DEFAULT_BUTTON_LONGPRESS_MS;
  cfg.buttonLongTickMs        = CFG_DEFAULT_BUTTON_LONGTICK_MS;
  cfg.reedInsertDwellMs       = CFG_DEFAULT_REED_INSERT_DWELL_MS;
  cfg.reedRemoveDwellMs       = CFG_DEFAULT_REED_REMOVE_DWELL_MS;
  cfg.statusLedDimDuty        = CFG_DEFAULT_STATUS_LED_DIM_DUTY;
  // Identity + secrets default to empty — first-boot setup populates them.
  strncpy(cfg.hostname, "mistmaker", CFG_HOSTNAME_MAX);
  cfg.hostname[CFG_HOSTNAME_MAX] = '\0';
  cfg.adminPasswordHash[0] = '\0';
  cfg.otaPassword[0] = '\0';
}

// --------------------------------------------------------------------------
// Blob ↔ struct conversion
// --------------------------------------------------------------------------

static void packBlob(CfgBlob& b) {
  b.version                = CONFIG_VERSION;
  b.ledBreathPeak          = cfg.ledBreathPeak;
  b.ledBreathPeriodMs      = cfg.ledBreathPeriodMs;
  b.ledBreathLow           = cfg.ledBreathLow;
  b.waveBaseLevel          = cfg.waveBaseLevel;
  b.waveSwellPeak          = cfg.waveSwellPeak;
  b.wavePeriodMs           = cfg.wavePeriodMs;
  b.ledCrossfadeMs         = cfg.ledCrossfadeMs;
  b.ledScaleStepPerTick    = cfg.ledScaleStepPerTick;
  b.mistDutyMax            = cfg.mistDutyMax;
  b.mistDutyMin            = cfg.mistDutyMin;
  b.levelDefault           = cfg.levelDefault;
  b.mistWaveTroughQ8       = cfg.mistWaveTroughQ8;
  b.levelSmoothTickMs      = cfg.levelSmoothTickMs;
  b.levelSmoothStepUp      = cfg.levelSmoothStepUp;
  b.levelSmoothStepDn      = cfg.levelSmoothStepDn;
  b.levelSmoothStepUpFast  = cfg.levelSmoothStepUpFast;
  b.levelRampTickMs        = cfg.levelRampTickMs;
  b.levelRampStep          = cfg.levelRampStep;
  b.buttonDebounceMs       = cfg.buttonDebounceMs;
  b.buttonLongPressMs      = cfg.buttonLongPressMs;
  b.buttonLongTickMs       = cfg.buttonLongTickMs;
  b.reedInsertDwellMs      = cfg.reedInsertDwellMs;
  b.reedRemoveDwellMs      = cfg.reedRemoveDwellMs;
  b.statusLedDimDuty       = cfg.statusLedDimDuty;
}

static void unpackBlob(const CfgBlob& b) {
  cfg.ledBreathPeak          = b.ledBreathPeak;
  cfg.ledBreathPeriodMs      = b.ledBreathPeriodMs;
  cfg.ledBreathLow           = b.ledBreathLow;
  cfg.waveBaseLevel          = b.waveBaseLevel;
  cfg.waveSwellPeak          = b.waveSwellPeak;
  cfg.wavePeriodMs           = b.wavePeriodMs;
  cfg.ledCrossfadeMs         = b.ledCrossfadeMs;
  cfg.ledScaleStepPerTick    = b.ledScaleStepPerTick;
  cfg.mistDutyMax            = b.mistDutyMax;
  cfg.mistDutyMin            = b.mistDutyMin;
  cfg.levelDefault           = b.levelDefault;
  cfg.mistWaveTroughQ8       = b.mistWaveTroughQ8;
  cfg.levelSmoothTickMs      = b.levelSmoothTickMs;
  cfg.levelSmoothStepUp      = b.levelSmoothStepUp;
  cfg.levelSmoothStepDn      = b.levelSmoothStepDn;
  cfg.levelSmoothStepUpFast  = b.levelSmoothStepUpFast;
  cfg.levelRampTickMs        = b.levelRampTickMs;
  cfg.levelRampStep          = b.levelRampStep;
  cfg.buttonDebounceMs       = b.buttonDebounceMs;
  cfg.buttonLongPressMs      = b.buttonLongPressMs;
  cfg.buttonLongTickMs       = b.buttonLongTickMs;
  cfg.reedInsertDwellMs      = b.reedInsertDwellMs;
  cfg.reedRemoveDwellMs      = b.reedRemoveDwellMs;
  cfg.statusLedDimDuty       = b.statusLedDimDuty;
}

// --------------------------------------------------------------------------
// NVS init / save
// --------------------------------------------------------------------------

// Helper: write one NVS string and close. Returns false on NVS open failure.
static bool nvsPutString(const char* key, const char* value) {
  Preferences p;
  if (!p.begin(NS, /*readOnly=*/false)) return false;
  p.putString(key, value);
  p.end();
  return true;
}

void configInit() {
  configResetDefaults();

  Preferences p;
  if (!p.begin(NS, /*readOnly=*/true)) {
    Serial.println("[CFG] NVS namespace empty — using firmware defaults");
    return;
  }

  // 1. Tunables blob.
  CfgBlob blob;
  const size_t got = p.getBytes(KEY_BLOB, &blob, sizeof(blob));
  if (got == sizeof(blob) && blob.version == CONFIG_VERSION) {
    unpackBlob(blob);
    Serial.println("[CFG] loaded tunables from NVS");
  } else if (got == 0) {
    Serial.println("[CFG] no saved tunables yet — using defaults");
  } else {
    Serial.printf("[CFG] saved blob mismatch (got=%u expect=%u, ver=%u expect=%u) — using defaults\n",
                  (unsigned)got, (unsigned)sizeof(blob),
                  got >= 1 ? blob.version : 0, CONFIG_VERSION);
  }

  // 2. Identity + secrets.
  const String host  = p.getString(KEY_HOST, "mistmaker");
  const String admin = p.getString(KEY_ADMIN_PW, "");
  const String ota   = p.getString(KEY_OTA_PW,   "");
  strncpy(cfg.hostname,           host.c_str(),  CFG_HOSTNAME_MAX);
  cfg.hostname[CFG_HOSTNAME_MAX] = '\0';
  strncpy(cfg.adminPasswordHash,  admin.c_str(), CFG_SHA256_HEX_LEN);
  cfg.adminPasswordHash[CFG_SHA256_HEX_LEN] = '\0';
  strncpy(cfg.otaPassword,        ota.c_str(),   CFG_OTA_PWD_MAX);
  cfg.otaPassword[CFG_OTA_PWD_MAX] = '\0';

  p.end();
}

bool configSave() {
  Preferences p;
  if (!p.begin(NS, /*readOnly=*/false)) {
    Serial.println("[CFG] NVS open (rw) failed");
    return false;
  }
  CfgBlob blob;
  packBlob(blob);
  const size_t put = p.putBytes(KEY_BLOB, &blob, sizeof(blob));
  p.end();
  if (put != sizeof(blob)) {
    Serial.printf("[CFG] save failed: wrote %u / %u bytes\n",
                  (unsigned)put, (unsigned)sizeof(blob));
    return false;
  }
  Serial.println("[CFG] saved");
  return true;
}

bool configSetHostname(const char* name) {
  if (!name) return false;
  const size_t n = strlen(name);
  if (n == 0 || n > CFG_HOSTNAME_MAX) return false;
  // Hostname must be DNS-safe: letters / digits / hyphen, not starting with -.
  for (size_t i = 0; i < n; ++i) {
    const char c = name[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                 || (c >= '0' && c <= '9') || c == '-';
    if (!ok) return false;
  }
  if (name[0] == '-' || name[n - 1] == '-') return false;
  strncpy(cfg.hostname, name, CFG_HOSTNAME_MAX);
  cfg.hostname[CFG_HOSTNAME_MAX] = '\0';
  return nvsPutString(KEY_HOST, cfg.hostname);
}

bool configSetAdminPassword(const char* pwd) {
  if (!pwd) return false;
  const size_t n = strlen(pwd);
  if (n < 4 || n > 64) return false;       // min 4 chars (low bar; let's not get in the way)
  configSha256Hex(pwd, n, cfg.adminPasswordHash);
  return nvsPutString(KEY_ADMIN_PW, cfg.adminPasswordHash);
}

bool configCheckAdminPassword(const char* pwd) {
  if (!pwd) return false;
  if (cfg.adminPasswordHash[0] == '\0') return false;  // no password set
  char hex[CFG_SHA256_HEX_LEN + 1];
  configSha256Hex(pwd, strlen(pwd), hex);
  return strcmp(hex, cfg.adminPasswordHash) == 0;
}

bool configSetOtaPassword(const char* pwd) {
  if (!pwd) return false;
  const size_t n = strlen(pwd);
  if (n > CFG_OTA_PWD_MAX) return false;
  strncpy(cfg.otaPassword, pwd, CFG_OTA_PWD_MAX);
  cfg.otaPassword[CFG_OTA_PWD_MAX] = '\0';
  return nvsPutString(KEY_OTA_PW, cfg.otaPassword);
}

// --------------------------------------------------------------------------
// Field updates (used by /api/config). Each entry validates its own range.
// --------------------------------------------------------------------------

// `name` is the runtime field-name argument (the function's parameter).
// `member` is the Config struct member identifier we want to assign to.
// `key` is the string literal we compare `name` against.
#define SET_U8(member, key, lo, hi) \
  if (strcmp(name, (key)) == 0) { \
    if (value < (lo) || value > (hi)) return false; \
    cfg.member = uint8_t(value); return true; }
#define SET_U16(member, key, lo, hi) \
  if (strcmp(name, (key)) == 0) { \
    if (value < (lo) || value > (hi)) return false; \
    cfg.member = uint16_t(value); return true; }

bool configSetField(const char* name, long value) {
  if (!name) return false;
  // LED BREATH
  SET_U8 (ledBreathPeak,         "ledBreathPeak",       0, 255);
  SET_U16(ledBreathPeriodMs,     "ledBreathPeriodMs",   500, 30000);
  SET_U8 (ledBreathLow,          "ledBreathLow",        0, 255);
  // LED WAVE
  SET_U8 (waveBaseLevel,         "waveBaseLevel",       0, 255);
  SET_U8 (waveSwellPeak,         "waveSwellPeak",       0, 255);
  SET_U16(wavePeriodMs,          "wavePeriodMs",        500, 60000);
  SET_U16(ledCrossfadeMs,        "ledCrossfadeMs",      0, 10000);
  SET_U8 (ledScaleStepPerTick,   "ledScaleStepPerTick", 1, 255);
  // Mist. Hardware-safe cap is 200 (~78% duty). Above that the piezo's peak
  // current at 108.7 kHz exceeds the TPS61023 boost converter's ~2 A current
  // limit, the boost goes into hiccup mode, the battery rail develops large
  // ripple, and WiFi browns out — the device becomes unreachable on the LAN.
  // Bench-validated: 200 holds stable, 220+ triggers the cascade.
  SET_U8 (mistDutyMax,           "mistDutyMax",         0, 200);
  SET_U8 (mistDutyMin,           "mistDutyMin",         0, 200);
  SET_U8 (levelDefault,          "levelDefault",        0, 255);
  SET_U16(mistWaveTroughQ8,      "mistWaveTroughQ8",    0, 256);
  // Smoother
  SET_U16(levelSmoothTickMs,     "levelSmoothTickMs",   1, 1000);
  SET_U8 (levelSmoothStepUp,     "levelSmoothStepUp",   1, 255);
  SET_U8 (levelSmoothStepDn,     "levelSmoothStepDn",   1, 255);
  SET_U8 (levelSmoothStepUpFast, "levelSmoothStepUpFast", 1, 255);
  SET_U16(levelRampTickMs,       "levelRampTickMs",     1, 1000);
  SET_U8 (levelRampStep,         "levelRampStep",       1, 255);
  // Button + reed
  SET_U16(buttonDebounceMs,      "buttonDebounceMs",    1, 1000);
  SET_U16(buttonLongPressMs,     "buttonLongPressMs",   50, 5000);
  SET_U16(buttonLongTickMs,      "buttonLongTickMs",    1, 1000);
  SET_U16(reedInsertDwellMs,     "reedInsertDwellMs",   0, 5000);
  SET_U16(reedRemoveDwellMs,     "reedRemoveDwellMs",   0, 5000);
  // Status LED
  SET_U8 (statusLedDimDuty,      "statusLedDimDuty",    0, 255);
  return false;  // unknown name
}

#undef SET_U8
#undef SET_U16

// --------------------------------------------------------------------------
// JSON serialization (no library dep; small enough to hand-roll).
// --------------------------------------------------------------------------

#define APPEND(fmt, ...) do { \
    if (n >= cap) return 0; \
    const int w = snprintf(out + n, cap - n, fmt, ##__VA_ARGS__); \
    if (w < 0) return 0; \
    n += size_t(w); \
    if (n >= cap) return 0; \
  } while (0)

size_t configToJson(char* out, size_t cap) {
  if (!out || cap == 0) return 0;
  size_t n = 0;
  APPEND("{");
  APPEND("\"ledBreathPeak\":%u,",        cfg.ledBreathPeak);
  APPEND("\"ledBreathPeriodMs\":%u,",    cfg.ledBreathPeriodMs);
  APPEND("\"ledBreathLow\":%u,",         cfg.ledBreathLow);
  APPEND("\"waveBaseLevel\":%u,",        cfg.waveBaseLevel);
  APPEND("\"waveSwellPeak\":%u,",        cfg.waveSwellPeak);
  APPEND("\"wavePeriodMs\":%u,",         cfg.wavePeriodMs);
  APPEND("\"ledCrossfadeMs\":%u,",       cfg.ledCrossfadeMs);
  APPEND("\"ledScaleStepPerTick\":%u,",  cfg.ledScaleStepPerTick);
  APPEND("\"mistDutyMax\":%u,",          cfg.mistDutyMax);
  APPEND("\"mistDutyMin\":%u,",          cfg.mistDutyMin);
  APPEND("\"levelDefault\":%u,",         cfg.levelDefault);
  APPEND("\"mistWaveTroughQ8\":%u,",     cfg.mistWaveTroughQ8);
  APPEND("\"levelSmoothTickMs\":%u,",    cfg.levelSmoothTickMs);
  APPEND("\"levelSmoothStepUp\":%u,",    cfg.levelSmoothStepUp);
  APPEND("\"levelSmoothStepDn\":%u,",    cfg.levelSmoothStepDn);
  APPEND("\"levelSmoothStepUpFast\":%u,",cfg.levelSmoothStepUpFast);
  APPEND("\"levelRampTickMs\":%u,",      cfg.levelRampTickMs);
  APPEND("\"levelRampStep\":%u,",        cfg.levelRampStep);
  APPEND("\"buttonDebounceMs\":%u,",     cfg.buttonDebounceMs);
  APPEND("\"buttonLongPressMs\":%u,",    cfg.buttonLongPressMs);
  APPEND("\"buttonLongTickMs\":%u,",     cfg.buttonLongTickMs);
  APPEND("\"reedInsertDwellMs\":%u,",    cfg.reedInsertDwellMs);
  APPEND("\"reedRemoveDwellMs\":%u,",    cfg.reedRemoveDwellMs);
  APPEND("\"statusLedDimDuty\":%u,",     cfg.statusLedDimDuty);
  APPEND("\"hostname\":\"%s\"",          cfg.hostname);
  APPEND("}");
  return n;
}

#undef APPEND
