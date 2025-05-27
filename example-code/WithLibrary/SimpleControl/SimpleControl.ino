// #include "../src/MistMaker.h"
#include <MistMaker.h>
const int MIST_OUTPUT_PIN = D1;
const int CURRENT_SENSE_PIN = D2;
const int EN_PIN = D3;
const int BUTTON_PIN = D6;
const int LED_PIN = D7;

MistMaker mist(MIST_OUTPUT_PIN, EN_PIN, CURRENT_SENSE_PIN, LED_PIN);

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUTTON_PIN, INPUT);
  mist.begin();

  Serial.println("MistMaker Library Example Ready.");
}

void loop() {
  if (digitalRead(BUTTON_PIN) == HIGH) {
    mist.toggle();
    delay(300); // debounce
  }

  if (millis() - millis() > 5000) {
    mist.printStatus();
  }
}