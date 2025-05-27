#include <MistMaker.h>

const int MIST_OUTPUT_PIN = D1;
const int CURRENT_SENSE_PIN = D2;
const int EN_PIN = D3;
const int LED_PIN = D7;

MistMaker mist(MIST_OUTPUT_PIN, EN_PIN, CURRENT_SENSE_PIN, LED_PIN);

void setup() {
  Serial.begin(115200);
  mist.begin();
  Serial.println("Blink example: toggling mist every 2 seconds");
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  mist.turnOn();
  digitalWrite(LED_BUILTIN, LOW);
  delay(2000); // Mist ON for 2 seconds

  mist.turnOff();
    digitalWrite(LED_BUILTIN, HIGH);
  delay(2000); // Mist OFF for 2 seconds
}