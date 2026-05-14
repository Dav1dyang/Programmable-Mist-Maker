// Block Kit V0.1 — BringUp sketch.
//
// Bare-bones per-feature verification. No state machine, no animations, no
// smoother. Press the button -> mist + all 14 LEDs ON at full. Press again
// -> OFF. Serial commands run an LED chase or dump a current-sense reading.
// This is what to flash when you need to answer "did the button wire up?"
// or "is the INA180 reading anything sane?".
//
// For the full UX (BREATH/WAVE animations, smoother, reed dock detection,
// WiFi/OTA/web config) flash ../BlockKit_Production/ instead.

#include <Wire.h>
#include <Adafruit_IS31FL3731.h>
#include "pins.h"

static Adafruit_IS31FL3731 g_is31;
static bool     g_ledReady   = false;

// Toggled by button press or `t` command. Drives mist + LEDs together.
static bool     g_on         = false;

// Button debounce state.
static bool     g_btnRaw     = false;
static bool     g_btnDeb     = false;
static uint32_t g_btnEdgeMs  = 0;

// Periodic [STAT] line timing.
static uint32_t g_lastStatMs = 0;

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

static inline float adcToMa(uint16_t raw) {
  const float volts = (float(raw) * 3.3f) / 4095.0f;
  return (volts * 1000.0f) / CURRENT_SENSE_FACTOR;
}

static void ledsAll(uint8_t pwm) {
  if (!g_ledReady) return;
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    g_is31.setLEDPWM(LED_MAP[i], pwm, LED_IS31_FRAME);
  }
}

static void applyOutputs() {
  if (g_on) {
    digitalWrite(PIN_BOOST_EN, HIGH);
    delayMicroseconds(500);              // brief settle for the 5 V rail
    ledcWrite(PIN_MIST_PWM, MIST_DUTY_FULL);
    ledcWrite(PIN_STATUS_LED, 0);        // D7 off while running
    ledsAll(LED_FIXED_ON_PWM);
    Serial.println("[OUT] ON  -> boost=HIGH mist=127 (50%) leds=ON d7=off");
  } else {
    ledcWrite(PIN_MIST_PWM, 0);
    digitalWrite(PIN_MIST_PWM, LOW);     // safety belt
    digitalWrite(PIN_BOOST_EN, LOW);
    ledcWrite(PIN_STATUS_LED, STATUS_LED_DIM_DUTY);
    ledsAll(0);
    Serial.println("[OUT] OFF -> boost=LOW  mist=0       leds=off d7=dim");
  }
}

static void toggle() {
  g_on = !g_on;
  applyOutputs();
}

// --------------------------------------------------------------------------
// LED chase (`w` command)
// --------------------------------------------------------------------------

static void ledWalk() {
  if (!g_ledReady) {
    Serial.println("[LED] walk: IS31FL3731 not ready");
    return;
  }
  Serial.println("[LED] walk: 14 LEDs, 1 s each, top->bottom");
  for (uint8_t i = 0; i < LED_COUNT; ++i) {
    ledsAll(0);
    g_is31.setLEDPWM(LED_MAP[i], LED_WALK_PWM, LED_IS31_FRAME);
    Serial.print("[LED] walk i=");
    Serial.print(i);
    Serial.print(" lednum=");
    Serial.println(LED_MAP[i]);
    delay(1000);
  }
  ledsAll(0);
  // After the chase, restore whichever state we were in.
  applyOutputs();
}

// --------------------------------------------------------------------------
// Current-sense readout (`c` command)
// --------------------------------------------------------------------------

static void currentDump() {
  uint32_t sum = 0;
  uint16_t lo = 0xFFFF, hi = 0;
  for (uint16_t i = 0; i < 100; ++i) {
    const uint16_t r = analogRead(PIN_CURRENT_ADC);
    sum += r;
    if (r < lo) lo = r;
    if (r > hi) hi = r;
    delayMicroseconds(200);
  }
  const uint16_t mean = uint16_t(sum / 100);
  Serial.print("[CUR] adc mean=");
  Serial.print(mean);
  Serial.print(" min=");
  Serial.print(lo);
  Serial.print(" max=");
  Serial.print(hi);
  Serial.print("  mA mean=");
  Serial.print(adcToMa(mean), 1);
  Serial.print(" min=");
  Serial.print(adcToMa(lo), 1);
  Serial.print(" max=");
  Serial.println(adcToMa(hi), 1);
}

// --------------------------------------------------------------------------
// Button polling (50 ms debounce, edge -> toggle)
// --------------------------------------------------------------------------

static void buttonPoll() {
  const bool raw = (digitalRead(PIN_BUTTON) == HIGH);
  const uint32_t now = millis();
  if (raw != g_btnRaw) {
    g_btnRaw = raw;
    g_btnEdgeMs = now;
    return;
  }
  if (now - g_btnEdgeMs < BUTTON_DEBOUNCE_MS) return;
  if (raw == g_btnDeb) return;
  g_btnDeb = raw;
  if (raw) {
    Serial.println("[BTN] press");
    toggle();
  }
}

// --------------------------------------------------------------------------
// Periodic [STAT] line: btn raw, reed raw, mist state, ADC mA.
// --------------------------------------------------------------------------

static void statTick() {
  const uint32_t now = millis();
  if (now - g_lastStatMs < 250) return;
  g_lastStatMs = now;
  const uint16_t adc = analogRead(PIN_CURRENT_ADC);
  Serial.print("[STAT] btn=");      Serial.print(digitalRead(PIN_BUTTON));
  Serial.print(" reed=");           Serial.print(digitalRead(PIN_REED) == LOW ? 1 : 0);
  Serial.print(" mist=");           Serial.print(g_on ? 1 : 0);
  Serial.print(" adc=");            Serial.print(adc);
  Serial.print(" mA=");             Serial.println(adcToMa(adc), 1);
}

// --------------------------------------------------------------------------
// Serial commands
// --------------------------------------------------------------------------

static void printHelp() {
  Serial.println(F("[CMD] BringUp commands (one char per line):"));
  Serial.println(F("  t  - toggle mist + LEDs (same as button)"));
  Serial.println(F("  w  - LED chase (~14 s, blocks)"));
  Serial.println(F("  c  - current-sense readout (mean / min / max of 100 reads)"));
  Serial.println(F("  h  - this help"));
}

static void pollSerial() {
  while (Serial.available()) {
    const char c = char(Serial.read());
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
    switch (c) {
      case 't': toggle(); break;
      case 'w': ledWalk(); break;
      case 'c': currentDump(); break;
      case 'h': printHelp(); break;
      default:
        Serial.print("[CMD] unknown: ");
        Serial.println(c);
    }
  }
}

// --------------------------------------------------------------------------
// Arduino entry points
// --------------------------------------------------------------------------

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);                              // USB-CDC enumeration on XIAO ESP32-C6
  Serial.println();
  Serial.println("[APP] Block Kit V0.1 BringUp (per-feature verification)");

  // SAFETY: boost rail LOW before anything else can drive D3.
  pinMode(PIN_BOOST_EN, OUTPUT);
  digitalWrite(PIN_BOOST_EN, LOW);

  Serial.print("[APP] PIN_MIST_PWM=D0 (GPIO ");    Serial.print(PIN_MIST_PWM);    Serial.println(")");
  Serial.print("[APP] PIN_BOOST_EN=D3 (GPIO ");    Serial.print(PIN_BOOST_EN);    Serial.println(")");
  Serial.print("[APP] PIN_CURRENT_ADC=D2 (GPIO "); Serial.print(PIN_CURRENT_ADC); Serial.println(")");
  Serial.print("[APP] PIN_BUTTON=D6 (GPIO ");      Serial.print(PIN_BUTTON);      Serial.println(")");
  Serial.print("[APP] PIN_STATUS_LED=D7 (GPIO ");  Serial.print(PIN_STATUS_LED);  Serial.println(")");
  Serial.print("[APP] PIN_REED=D10 (GPIO ");       Serial.print(PIN_REED);        Serial.println(")");

  // Mist PWM (off at boot).
  ledcAttach(PIN_MIST_PWM, MIST_FREQ_HZ, MIST_PWM_RES);
  ledcWrite(PIN_MIST_PWM, 0);

  // Status LED (D7) at dim baseline.
  ledcAttach(PIN_STATUS_LED, STATUS_LED_FREQ_HZ, STATUS_LED_RES);
  ledcWrite(PIN_STATUS_LED, STATUS_LED_DIM_DUTY);

  // Button + reed (PCB has 10 k pull-down on D6 and the reed pulls D10 LOW).
  pinMode(PIN_BUTTON, INPUT_PULLDOWN);
  pinMode(PIN_REED, INPUT_PULLUP);

  // ADC pin — bench-validated recipe: pinMode INPUT, no analogReadResolution,
  // no analogSetPinAttenuation. C6 v3.x returns 0 if you tweak those.
  pinMode(PIN_CURRENT_ADC, INPUT);

  // IS31FL3731 LED ring.
  Wire.begin();
  g_ledReady = g_is31.begin(LED_IS31_ADDR);
  if (!g_ledReady) {
    Serial.println("[LED] IS31FL3731 not found at 0x74 — LED ring tests will skip");
  } else {
    Serial.println("[LED] IS31FL3731 ok");
    ledsAll(0);
  }

  applyOutputs();   // emit initial OFF state
  printHelp();
}

void loop() {
  pollSerial();
  buttonPoll();
  statTick();
}
