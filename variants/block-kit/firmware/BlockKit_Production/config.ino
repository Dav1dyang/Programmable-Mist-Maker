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

// The exact byte layout we persist for KEY_BLOB. Plain POD, no padding
// surprises across rebuilds because every field is byte/u16-aligned and we
// don't include the C-string fields here.
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
  static const char* HEX = "0123456789abcdef";
  for (int i = 0; i < 32; ++i) {
    outHex[i * 2 + 0] = HEX[(digest[i] >> 4) & 0xF];
    outHex[i * 2 + 1] = HEX[digest[i] & 0xF];
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
  strncpy(cfg.hostname, "blockkit", CFG_HOSTNAME_MAX);
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
  const String host  = p.getString(KEY_HOST, "blockkit");
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
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putString(KEY_HOST, cfg.hostname);
  p.end();
  return true;
}

bool configSetAdminPassword(const char* pwd) {
  if (!pwd) return false;
  const size_t n = strlen(pwd);
  if (n < 4 || n > 64) return false;       // min 4 chars (low bar; let's not get in the way)
  configSha256Hex(pwd, n, cfg.adminPasswordHash);
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putString(KEY_ADMIN_PW, cfg.adminPasswordHash);
  p.end();
  return true;
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
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putString(KEY_OTA_PW, cfg.otaPassword);
  p.end();
  return true;
}

// --------------------------------------------------------------------------
// Field updates (used by /api/config). Each entry validates its own range.
// --------------------------------------------------------------------------

#define SET_U8(field, name, lo, hi) \
  if (strcmp(field, name) == 0) { \
    if (value < (lo) || value > (hi)) return false; \
    cfg.field = uint8_t(value); return true; }
#define SET_U16(field, name, lo, hi) \
  if (strcmp(field, name) == 0) { \
    if (value < (lo) || value > (hi)) return false; \
    cfg.field = uint16_t(value); return true; }

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
  // Mist
  SET_U8 (mistDutyMax,           "mistDutyMax",         0, 255);
  SET_U8 (mistDutyMin,           "mistDutyMin",         0, 255);
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
