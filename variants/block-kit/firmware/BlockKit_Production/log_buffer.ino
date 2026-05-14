// Tiny RAM ring buffer that mirrors high-value Serial output.
//
// Why this exists: when the device is on the user's bench WiFi instead of
// tethered to a laptop, the Debug tab of the web UI needs to show recent
// log lines. We don't intercept every Serial.print — too invasive — but we
// do offer a `logPrintln(...)` / `logPrintf(...)` shim that ALSO writes to
// the ring. New code (web_server, ota, wifi_setup, config) calls these.
// Pre-existing high-traffic lines (`[PLOT]`) stay on Serial only.

#include "pins.h"

// Sized for ~6 KB of RAM at most — well within ESP32-C6's 512 KB but small
// enough that the rest of the firmware is unaffected.
static constexpr uint16_t LOG_LINES = 80;
static constexpr uint16_t LOG_LINE_MAX = 80;   // chars including trailing NUL

static char     g_log[LOG_LINES][LOG_LINE_MAX];
static uint16_t g_logHead = 0;        // index of OLDEST line
static uint16_t g_logCount = 0;       // 0..LOG_LINES

void logInit() {
  for (uint16_t i = 0; i < LOG_LINES; ++i) g_log[i][0] = '\0';
  g_logHead = 0;
  g_logCount = 0;
}

static inline void logAppendLine(const char* s) {
  const uint16_t slot = (g_logHead + g_logCount) % LOG_LINES;
  strncpy(g_log[slot], s, LOG_LINE_MAX - 1);
  g_log[slot][LOG_LINE_MAX - 1] = '\0';
  if (g_logCount < LOG_LINES) {
    ++g_logCount;
  } else {
    g_logHead = (g_logHead + 1) % LOG_LINES;   // ring wraps, overwrite oldest
  }
}

void logPrintln(const char* s) {
  Serial.println(s);
  logAppendLine(s ? s : "");
}

void logPrintf(const char* fmt, ...) {
  char buf[LOG_LINE_MAX];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
  // Strip trailing newline if present so each ring slot is one line.
  const size_t n = strlen(buf);
  if (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
    char trimmed[LOG_LINE_MAX];
    strncpy(trimmed, buf, n - 1);
    trimmed[n - 1] = '\0';
    logAppendLine(trimmed);
  } else {
    logAppendLine(buf);
  }
}

// Copy the buffer into `out` (capacity `cap`) as newline-separated text,
// oldest line first. Returns bytes written (excluding trailing NUL).
size_t logSnapshot(char* out, size_t cap) {
  if (!out || cap == 0) return 0;
  size_t n = 0;
  for (uint16_t i = 0; i < g_logCount; ++i) {
    const uint16_t slot = (g_logHead + i) % LOG_LINES;
    const size_t len = strlen(g_log[slot]);
    if (n + len + 1 >= cap) break;
    memcpy(out + n, g_log[slot], len);
    n += len;
    out[n++] = '\n';
  }
  if (n < cap) out[n] = '\0';
  return n;
}
