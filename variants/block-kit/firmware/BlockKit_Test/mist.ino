// Mist drive — 108.7 kHz PWM on D0, gated by D3 boost enable.
// Power-gates the 5 V rail: D3 HIGH only while RUNNING, LOW while IDLE.

#include "pins.h"

static bool g_mistRunning = false;

void mistInit() {
  pinMode(PIN_BOOST_EN, OUTPUT);
  digitalWrite(PIN_BOOST_EN, LOW);

  // Arduino-ESP32 v3.x LEDC API: attach to a pin directly with freq + resolution.
  ledcAttach(PIN_MIST_PWM, MIST_FREQ_HZ, MIST_PWM_RES);
  ledcWrite(PIN_MIST_PWM, 0);
}

void mistOn() {
  if (g_mistRunning) return;
  digitalWrite(PIN_BOOST_EN, HIGH);     // bring up 5 V rail first
  delayMicroseconds(500);               // brief settle before driving the disc
  ledcWrite(PIN_MIST_PWM, MIST_DUTY_RUN);
  g_mistRunning = true;
  Serial.println("[MIST] on");
}

void mistOff() {
  if (!g_mistRunning) return;
  ledcWrite(PIN_MIST_PWM, 0);
  digitalWrite(PIN_MIST_PWM, LOW);      // safety belt — force D0 low after PWM stop
  digitalWrite(PIN_BOOST_EN, LOW);      // drop 5 V rail to save energy
  g_mistRunning = false;
  Serial.println("[MIST] off");
}

bool mistIsRunning() {
  return g_mistRunning;
}
