// WiFi onboarding via tzapu's WiFiManager.
// On boot: autoConnect() joins saved STA; on first boot or gone-AP it spins
// up "MistMaker-Setup-XXXX" (WPA2, pwd `mistmaker-setup`) as a captive portal.
// WiFiManager handles iOS/Android/Windows captive-portal probes for us.

#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include "pins.h"
#include "config.h"

static WiFiManager       g_wm;
static WiFiManagerParameter* g_paramAdminPwd = nullptr;
static bool              g_setupMode      = false;
static bool              g_credentialsSaved = false;
static uint32_t          g_lastReconnectMs = 0;

bool wifiIsSetupMode() { return g_setupMode; }
const char* wifiHostname() { return cfg.hostname; }

// --------------------------------------------------------------------------
// SSID helpers — append the last 4 hex chars of the WiFi MAC so two devices
// on the same bench don't collide.
// --------------------------------------------------------------------------

static const char* apSsid() {
  static char ssid[32] = {0};
  if (ssid[0]) return ssid;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(ssid, sizeof(ssid), "MistMaker-Setup-%02X%02X", mac[4], mac[5]);
  return ssid;
}

// --------------------------------------------------------------------------
// WiFiManager callbacks
// --------------------------------------------------------------------------

static void onConfigPortalStart(WiFiManager* /*wm*/) {
  g_setupMode = true;
  Serial.println("[WIFI] captive portal up");
  Serial.print  ("[WIFI] SSID: ");      Serial.println(apSsid());
  Serial.println("[WIFI] password: mistmaker-setup");
  Serial.println("[WIFI] join the AP then browse to 192.168.4.1 or any URL");
}

static void onCredentialsSaved() {
  g_credentialsSaved = true;
  Serial.println("[WIFI] credentials saved by user — rebooting into STA");
  // Persist the admin password if the user set one in the portal.
  if (g_paramAdminPwd) {
    const char* p = g_paramAdminPwd->getValue();
    if (p && strlen(p) >= 4) {
      if (configSetAdminPassword(p)) {
        configSetOtaPassword(p);   // OTA password mirrors admin by default
        Serial.println("[WIFI] admin password set");
      } else {
        Serial.println("[WIFI] admin password rejected (min 4 chars)");
      }
    }
  }
}

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

void wifiInit() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg.hostname);

  // Custom parameter: admin password. Empty input on a normal reconnect
  // leaves the stored password untouched.
  static char adminPwdBuf[33] = "";       // never auto-fill the box
  g_paramAdminPwd = new WiFiManagerParameter(
      "adminPwd",                          // HTML field id
      "Admin password (min 4 chars, used for web write + OTA)",
      adminPwdBuf, 32,
      "type='password' minlength='4' maxlength='32'");
  g_wm.addParameter(g_paramAdminPwd);

  g_wm.setConfigPortalTimeout(180);        // 3 minutes then give up
  g_wm.setConnectTimeout(20);              // 20 s STA join attempt per saved net
  g_wm.setHostname(cfg.hostname);
  g_wm.setShowInfoUpdate(false);
  g_wm.setAPCallback(onConfigPortalStart);
  g_wm.setSaveConfigCallback(onCredentialsSaved);
  g_wm.setTitle("Mist Maker Setup");
  g_wm.setDarkMode(true);

  Serial.println("[WIFI] autoConnect...");
  const bool ok = g_wm.autoConnect(apSsid(), "mistmaker-setup");
  if (!ok) {
    Serial.println("[WIFI] autoConnect timeout — rebooting");
    delay(500);
    ESP.restart();
  }

  g_setupMode = false;
  Serial.printf("[WIFI] STA up: SSID=%s IP=%s RSSI=%d dBm\n",
                WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(),
                WiFi.RSSI());

  // mDNS — http://<hostname>.local in browsers, and lets the Arduino IDE
  // discover the device as a network port (Tools → Port).
  if (MDNS.begin(cfg.hostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[WIFI] mDNS: http://%s.local\n", cfg.hostname);
  } else {
    Serial.println("[WIFI] mDNS init failed");
  }
}

// Cheap reconnect-watcher. If the STA drops we let arduino-esp32's auto-
// reconnect handle it, but log the transition. No blocking here — the LED +
// mist loop must keep ticking even if WiFi is gone.
void wifiTick() {
  static bool wasConnected = true;
  const uint32_t now = millis();
  if (now - g_lastReconnectMs < 5000) return;
  g_lastReconnectMs = now;
  const bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected != wasConnected) {
    Serial.printf("[WIFI] %s\n", connected ? "reconnected" : "lost");
    wasConnected = connected;
  }
}

// Called from web_server.ino on /api/cmd/forget — wipes saved WiFi creds
// (NOT NVS config), then reboots into the captive portal.
void wifiForgetAndReboot() {
  Serial.println("[WIFI] forget — clearing saved credentials, rebooting");
  g_wm.resetSettings();
  delay(500);
  ESP.restart();
}
