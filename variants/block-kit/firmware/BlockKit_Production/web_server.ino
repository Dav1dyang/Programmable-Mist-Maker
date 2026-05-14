// Synchronous HTTP server backing the single-page web UI.
// GETs are open; POSTs require HTTP Basic against the admin password hash.
//
//   GET  /                  index page (PROGMEM HTML)
//   GET  /api/status        live status JSON
//   GET  /api/config        cfg snapshot (NEVER includes secrets)
//   POST /api/config        apply + save fields; body = {"field":"name","value":N}
//   POST /api/cmd/walk      run ledWalk on next loop
//   POST /api/cmd/leds      toggle hide/show LED ring (mist unaffected)
//   POST /api/cmd/scope     toggle scope mode
//   POST /api/cmd/plotmute  toggle [PLOT] CSV stream mute
//   POST /api/cmd/level     set runtime userLevel; body = {"value":0..255}
//   POST /api/cmd/state     force state idle/running; body = {"state":"..."}
//   POST /api/cmd/statled   override indicator LED; body = {"mode":"auto|on|off"}
//   POST /api/cmd/reboot    ESP.restart() after 250 ms
//   POST /api/cmd/forget    wipe WiFi creds + reboot into captive portal
//   POST /api/cmd/password  set new admin password; body = {"new":"..."}
//   GET  /api/events        Server-Sent Events stream (status every 250 ms)
//   GET  /api/log           recent log lines (plain text)
//   GET  /api/info          device info (mac, ip, ver, mDNS) — JSON

#include <WebServer.h>
#include <WiFi.h>
#include <mbedtls/base64.h>
#include "pins.h"
#include "config.h"
#include "web_ui.h"

// Forward decls — defined elsewhere in the sketch.
extern AppState appCurrentState();
extern uint8_t  appUserLevel();
extern uint8_t  appCurrentLevel();
extern void     appKickLedWalk();
extern bool     appLedsHidden();
extern void     appToggleLedsHidden();
extern void     appSetLevel(uint8_t);
extern void     appForceState(AppState);
extern void     appSetStatusLedOverride(int8_t);
extern int8_t   appStatusLedOverride();
extern bool     containerIsPresent();
extern bool     containerRawPresent();
extern bool     mistIsRunning();
extern float    currentMeanMa();
extern float    currentVarMa2();
extern void     currentSenseToggleScope();
extern void     currentSenseTogglePlotMute();
extern void     wifiForgetAndReboot();
extern bool     wifiIsSetupMode();
extern size_t   logSnapshot(char*, size_t);
extern Config   cfg;   // defined in config.ino

static WebServer  g_http(80);

// SSE client state. The synchronous WebServer only supports one streaming
// client at a time without getting clever; we track one fd and reject new
// /api/events GETs while it's still alive.
static WiFiClient g_sseClient;
static uint32_t   g_lastSseMs        = 0;
static constexpr uint32_t SSE_INTERVAL_MS = 250;

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

static bool requireAuth() {
  // Open access if no password has been set yet (first-boot grace period).
  // configCheckAdminPassword() returns false when the hash is empty, so we
  // gate writes only after the user has set up the device.
  if (cfg.adminPasswordHash[0] == '\0') return true;
  if (!g_http.authenticate("admin", "")) {
    // The synchronous WebServer can only check user:password pairs, but our
    // hash check is custom. Pull the raw Authorization header instead.
    if (!g_http.hasHeader("Authorization")) {
      g_http.sendHeader("WWW-Authenticate", "Basic realm=\"BlockKit\"");
      g_http.send(401, "application/json", "{\"error\":\"auth required\"}");
      return false;
    }
  }
  // Manual Basic decode — username is "admin", password gets sha256-checked.
  const String h = g_http.header("Authorization");
  const int sp = h.indexOf(' ');
  if (sp < 0) {
    g_http.send(401, "application/json", "{\"error\":\"bad auth header\"}");
    return false;
  }
  const String b64 = h.substring(sp + 1);
  // Decode base64 in place — base64 lengths are at most 4/3 of decoded length.
  // arduino-esp32 ships a `mbedtls_base64_decode`; use it.
  uint8_t buf[96];
  size_t  outLen = 0;
  if (mbedtls_base64_decode(buf, sizeof(buf) - 1, &outLen,
                            (const unsigned char*)b64.c_str(), b64.length()) != 0) {
    g_http.send(401, "application/json", "{\"error\":\"bad base64\"}");
    return false;
  }
  buf[outLen] = '\0';
  const char* pwd = (const char*)buf;
  const char* colon = strchr(pwd, ':');
  if (!colon) {
    g_http.send(401, "application/json", "{\"error\":\"bad cred fmt\"}");
    return false;
  }
  if (!configCheckAdminPassword(colon + 1)) {
    g_http.sendHeader("WWW-Authenticate", "Basic realm=\"BlockKit\"");
    g_http.send(401, "application/json", "{\"error\":\"wrong password\"}");
    return false;
  }
  return true;
}

// Local copy: `stateName` lives as `static` in BlockKit_Production.ino and
// Arduino concatenates all .ino files into one translation unit, so two
// `static const char* stateName(...)` definitions in the same TU collide.
// Renamed here to avoid the duplicate.
static const char* webStateName(AppState s) {
  switch (s) {
    case AppState::IDLE:                    return "IDLE";
    case AppState::RUNNING:                 return "RUNNING";
    case AppState::TRANSITION_FROM_RUNNING: return "XFADE_OUT";
  }
  return "?";
}

static size_t buildStatusJson(char* out, size_t cap) {
  return snprintf(out, cap,
    "{\"state\":\"%s\","
    "\"stateInt\":%u,"
    "\"btnRaw\":%d,"
    "\"reedRaw\":%d,"
    "\"reedPresent\":%d,"
    "\"mist\":%d,"
    "\"ledsHidden\":%d,"
    "\"setupMode\":%d,"
    "\"userLevel\":%u,"
    "\"currentLevel\":%u,"
    "\"meanMa\":%.1f,"
    "\"varMa2\":%.1f,"
    "\"uptimeMs\":%lu,"
    "\"freeHeap\":%u,"
    "\"rssi\":%d,"
    "\"statLedOverride\":%d}",
    webStateName(appCurrentState()),
    unsigned(appCurrentState()),
    digitalRead(PIN_BUTTON),
    digitalRead(PIN_REED) == LOW ? 1 : 0,
    containerIsPresent() ? 1 : 0,
    mistIsRunning() ? 1 : 0,
    appLedsHidden() ? 1 : 0,
    wifiIsSetupMode() ? 1 : 0,
    appUserLevel(),
    appCurrentLevel(),
    currentMeanMa(),
    currentVarMa2(),
    (unsigned long)millis(),
    unsigned(ESP.getFreeHeap()),
    (int)WiFi.RSSI(),
    int(appStatusLedOverride()));
}

// --------------------------------------------------------------------------
// JSON body parsing — tiny hand-rolled extractor (no library dep). Pulls
// `"key":VALUE` where VALUE is an unquoted integer or a quoted string.
// --------------------------------------------------------------------------

static bool jsonGetString(const String& body, const char* key, char* out, size_t cap) {
  String pat = String("\"") + key + "\"";
  int k = body.indexOf(pat);
  if (k < 0) return false;
  k = body.indexOf(':', k);
  if (k < 0) return false;
  int q1 = body.indexOf('"', k);
  if (q1 < 0) return false;
  int q2 = body.indexOf('"', q1 + 1);
  if (q2 < 0) return false;
  const String s = body.substring(q1 + 1, q2);
  strncpy(out, s.c_str(), cap - 1);
  out[cap - 1] = '\0';
  return true;
}

static bool jsonGetLong(const String& body, const char* key, long& out) {
  String pat = String("\"") + key + "\"";
  int k = body.indexOf(pat);
  if (k < 0) return false;
  k = body.indexOf(':', k);
  if (k < 0) return false;
  ++k;
  while (k < (int)body.length() && (body[k] == ' ' || body[k] == '\t')) ++k;
  int start = k;
  if (k < (int)body.length() && (body[k] == '-' || body[k] == '+')) ++k;
  while (k < (int)body.length() && body[k] >= '0' && body[k] <= '9') ++k;
  if (k == start) return false;
  out = body.substring(start, k).toInt();
  return true;
}

// --------------------------------------------------------------------------
// Route handlers
// --------------------------------------------------------------------------

static void handleRoot() {
  g_http.sendHeader("Cache-Control", "public, max-age=60");
  g_http.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void handleStatus() {
  char buf[512];
  buildStatusJson(buf, sizeof(buf));
  g_http.sendHeader("Cache-Control", "no-store");
  g_http.send(200, "application/json", buf);
}

static void handleConfigGet() {
  char buf[1024];
  configToJson(buf, sizeof(buf));
  g_http.sendHeader("Cache-Control", "no-store");
  g_http.send(200, "application/json", buf);
}

static void handleConfigPost() {
  if (!requireAuth()) return;
  const String body = g_http.arg("plain");
  char field[48];
  long value = 0;
  if (!jsonGetString(body, "field", field, sizeof(field)) ||
      !jsonGetLong(body, "value", value)) {
    g_http.send(400, "application/json",
                "{\"error\":\"body must be {\\\"field\\\":\\\"...\\\",\\\"value\\\":N}\"}");
    return;
  }
  if (!configSetField(field, value)) {
    g_http.send(400, "application/json", "{\"error\":\"unknown field or value out of range\"}");
    return;
  }
  if (!configSave()) {
    g_http.send(500, "application/json", "{\"error\":\"NVS save failed\"}");
    return;
  }
  Serial.printf("[CFG] set %s = %ld\n", field, value);
  char snap[1024];
  configToJson(snap, sizeof(snap));
  g_http.send(200, "application/json", snap);
}

static void handleInfo() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[384];
  snprintf(buf, sizeof(buf),
    "{\"hostname\":\"%s\","
    "\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
    "\"ip\":\"%s\","
    "\"ssid\":\"%s\","
    "\"rssi\":%d,"
    "\"firmware\":\"%s\","
    "\"buildDate\":\"%s %s\","
    "\"freeHeap\":%u,"
    "\"otaPort\":3232,"
    "\"hasAdminPwd\":%d}",
    cfg.hostname,
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
    WiFi.localIP().toString().c_str(),
    WiFi.SSID().c_str(),
    (int)WiFi.RSSI(),
    "BlockKit V0.1 Production",
    __DATE__, __TIME__,
    unsigned(ESP.getFreeHeap()),
    cfg.adminPasswordHash[0] ? 1 : 0);
  g_http.send(200, "application/json", buf);
}

static void handleLog() {
  // Up to ~6 KB of plain text; sized to match log_buffer's ring.
  static char buf[7000];
  const size_t n = logSnapshot(buf, sizeof(buf));
  g_http.setContentLength(n);
  g_http.send(200, "text/plain; charset=utf-8", "");
  if (n) g_http.client().write((const uint8_t*)buf, n);
}

static void handleCmdWalk() {
  if (!requireAuth()) return;
  appKickLedWalk();
  g_http.send(200, "application/json", "{\"ok\":true}");
}

static void handleCmdLeds() {
  if (!requireAuth()) return;
  appToggleLedsHidden();
  g_http.send(200, "application/json", "{\"ok\":true}");
}

static void handleCmdScope() {
  if (!requireAuth()) return;
  currentSenseToggleScope();
  g_http.send(200, "application/json", "{\"ok\":true}");
}

static void handleCmdPlotMute() {
  if (!requireAuth()) return;
  currentSenseTogglePlotMute();
  g_http.send(200, "application/json", "{\"ok\":true}");
}

// Live level set — mirrors serial 'vN'. Body: {"value": 0..255}.
static void handleCmdLevel() {
  if (!requireAuth()) return;
  const String body = g_http.arg("plain");
  long v = 0;
  if (!jsonGetLong(body, "value", v) || v < 0 || v > 255) {
    g_http.send(400, "application/json", "{\"error\":\"value must be 0..255\"}");
    return;
  }
  appSetLevel(uint8_t(v));
  Serial.printf("[WEB] level=%ld\n", v);
  g_http.send(200, "application/json", "{\"ok\":true}");
}

// Manual reed-state override. Body: {"state":"idle"|"running"}.
static void handleCmdState() {
  if (!requireAuth()) return;
  const String body = g_http.arg("plain");
  char state[16];
  if (!jsonGetString(body, "state", state, sizeof(state))) {
    g_http.send(400, "application/json", "{\"error\":\"missing 'state'\"}");
    return;
  }
  if (strcmp(state, "idle") == 0)         appForceState(AppState::IDLE);
  else if (strcmp(state, "running") == 0) appForceState(AppState::RUNNING);
  else {
    g_http.send(400, "application/json", "{\"error\":\"state must be 'idle' or 'running'\"}");
    return;
  }
  g_http.send(200, "application/json", "{\"ok\":true}");
}

// Indicator-LED override. Body: {"mode":"auto"|"on"|"off"}.
static void handleCmdStatLed() {
  if (!requireAuth()) return;
  const String body = g_http.arg("plain");
  char mode[8];
  if (!jsonGetString(body, "mode", mode, sizeof(mode))) {
    g_http.send(400, "application/json", "{\"error\":\"missing 'mode'\"}");
    return;
  }
  if      (strcmp(mode, "auto") == 0) appSetStatusLedOverride(-1);
  else if (strcmp(mode, "on")   == 0) appSetStatusLedOverride(1);
  else if (strcmp(mode, "off")  == 0) appSetStatusLedOverride(0);
  else {
    g_http.send(400, "application/json", "{\"error\":\"mode must be auto|on|off\"}");
    return;
  }
  g_http.send(200, "application/json", "{\"ok\":true}");
}

static void handleCmdReboot() {
  if (!requireAuth()) return;
  g_http.send(200, "application/json", "{\"ok\":true,\"rebooting\":250}");
  delay(250);
  ESP.restart();
}

static void handleCmdForget() {
  if (!requireAuth()) return;
  g_http.send(200, "application/json", "{\"ok\":true,\"forgetting\":500}");
  delay(500);
  wifiForgetAndReboot();
}

static void handleCmdPassword() {
  if (!requireAuth()) return;
  const String body = g_http.arg("plain");
  char newPwd[64];
  if (!jsonGetString(body, "new", newPwd, sizeof(newPwd))) {
    g_http.send(400, "application/json", "{\"error\":\"missing 'new' field\"}");
    return;
  }
  if (!configSetAdminPassword(newPwd)) {
    g_http.send(400, "application/json", "{\"error\":\"password rejected (min 4 chars)\"}");
    return;
  }
  configSetOtaPassword(newPwd);   // OTA mirrors admin
  Serial.println("[CFG] admin/OTA password changed via web");
  g_http.send(200, "application/json", "{\"ok\":true}");
}

// --------------------------------------------------------------------------
// SSE — Server-Sent Events stream
// --------------------------------------------------------------------------

static void handleEvents() {
  // Single subscriber model: replace any existing client.
  WiFiClient newClient = g_http.client();
  if (!newClient) return;
  if (g_sseClient && g_sseClient.connected()) {
    g_sseClient.stop();
  }
  g_sseClient = newClient;

  // Manually send SSE response headers.
  g_sseClient.println("HTTP/1.1 200 OK");
  g_sseClient.println("Content-Type: text/event-stream; charset=utf-8");
  g_sseClient.println("Cache-Control: no-store");
  g_sseClient.println("Connection: keep-alive");
  g_sseClient.println();
  g_sseClient.flush();
  // Immediate first frame so the UI populates without waiting 250 ms.
  char buf[512];
  const size_t n = buildStatusJson(buf, sizeof(buf));
  g_sseClient.print("data: ");
  g_sseClient.write((const uint8_t*)buf, n);
  g_sseClient.print("\n\n");
  g_lastSseMs = millis();
}

static void sseTick() {
  if (!g_sseClient) return;
  if (!g_sseClient.connected()) {
    g_sseClient.stop();
    return;
  }
  const uint32_t now = millis();
  if (now - g_lastSseMs < SSE_INTERVAL_MS) return;
  g_lastSseMs = now;
  char buf[512];
  const size_t n = buildStatusJson(buf, sizeof(buf));
  // SSE wire format: `data: <line>\n\n`. Each loop iteration sends one frame.
  g_sseClient.print("data: ");
  g_sseClient.write((const uint8_t*)buf, n);
  g_sseClient.print("\n\n");
}

// --------------------------------------------------------------------------
// Public init / tick
// --------------------------------------------------------------------------

void webInit() {
  // We need the Authorization header preserved (the default WebServer
  // strips most headers); enable it explicitly.
  const char* keepHeaders[] = {"Authorization"};
  g_http.collectHeaders(keepHeaders, 1);

  g_http.on("/",                    HTTP_GET,  handleRoot);
  g_http.on("/api/status",          HTTP_GET,  handleStatus);
  g_http.on("/api/config",          HTTP_GET,  handleConfigGet);
  g_http.on("/api/config",          HTTP_POST, handleConfigPost);
  g_http.on("/api/info",            HTTP_GET,  handleInfo);
  g_http.on("/api/log",             HTTP_GET,  handleLog);
  g_http.on("/api/events",          HTTP_GET,  handleEvents);
  g_http.on("/api/cmd/walk",        HTTP_POST, handleCmdWalk);
  g_http.on("/api/cmd/leds",        HTTP_POST, handleCmdLeds);
  g_http.on("/api/cmd/scope",       HTTP_POST, handleCmdScope);
  g_http.on("/api/cmd/plotmute",    HTTP_POST, handleCmdPlotMute);
  g_http.on("/api/cmd/level",       HTTP_POST, handleCmdLevel);
  g_http.on("/api/cmd/state",       HTTP_POST, handleCmdState);
  g_http.on("/api/cmd/statled",     HTTP_POST, handleCmdStatLed);
  g_http.on("/api/cmd/reboot",      HTTP_POST, handleCmdReboot);
  g_http.on("/api/cmd/forget",      HTTP_POST, handleCmdForget);
  g_http.on("/api/cmd/password",    HTTP_POST, handleCmdPassword);
  g_http.onNotFound([]() {
    g_http.send(404, "application/json", "{\"error\":\"not found\"}");
  });
  g_http.begin();
  Serial.println("[WEB] HTTP server up on :80");
}

void webHandle() {
  g_http.handleClient();
  sseTick();
}
