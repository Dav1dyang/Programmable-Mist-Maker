// ArduinoOTA wiring. Once STA is up the Arduino IDE auto-discovers the
// device as a network port (`Tools → Port → blockkit at 192.168.x.x (esp32)`)
// — pick it, hit Upload, the IDE prompts for the password set in the WiFi
// captive portal. Password mismatch silently rejects, just like Arduino OTA
// always does.
//
// SAFETY: the `onStart` callback hard-stops the mist (PWM 0, boost rail LOW,
// inhibit flag set) and blanks the LED ring before flash erase begins. The
// MOSFET gate is left in a known LOW state for the entire upload — no
// surprise misting if the upload is interrupted half way through.

#include <ArduinoOTA.h>
#include "pins.h"
#include "config.h"

// Forward decls — defined in BlockKit_Production.ino / mist.ino / led_driver.ino.
void mistHardStop();
void ledAllOff();
const char* wifiHostname();

void otaInit() {
  ArduinoOTA.setHostname(wifiHostname());
  if (cfg.otaPassword[0] != '\0') {
    ArduinoOTA.setPassword(cfg.otaPassword);
  } else {
    Serial.println("[OTA] WARNING: no OTA password set — any host on the LAN can flash");
  }

  ArduinoOTA.onStart([]() {
    const char* type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.printf("[OTA] upload start (%s) — mist locked, LEDs off\n", type);
    // SAFETY: cut the boost rail BEFORE the flash erase. If we crash or the
    // upload aborts mid-flight, the MOSFET gate stays LOW (10 k pull-down on
    // the PCB) so the piezo can't be driven.
    mistHardStop();
    digitalWrite(PIN_BOOST_EN, LOW);
    ledAllOff();
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] upload complete — rebooting");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // Print every 10 % so the serial log isn't flooded.
    static int lastPct = -1;
    const int pct = total ? int((uint64_t(progress) * 100) / total) : 0;
    if (pct / 10 != lastPct / 10) {
      Serial.printf("[OTA] %d%%\n", pct);
      lastPct = pct;
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    const char* reason = "unknown";
    switch (error) {
      case OTA_AUTH_ERROR:    reason = "auth (wrong password)"; break;
      case OTA_BEGIN_ERROR:   reason = "begin (flash too small?)"; break;
      case OTA_CONNECT_ERROR: reason = "connect"; break;
      case OTA_RECEIVE_ERROR: reason = "receive"; break;
      case OTA_END_ERROR:     reason = "end (invalid image?)"; break;
    }
    Serial.printf("[OTA] error: %s\n", reason);
  });

  ArduinoOTA.begin();
  Serial.printf("[OTA] ready @ %s:3232\n", wifiHostname());
}

void otaHandle() {
  ArduinoOTA.handle();
}
