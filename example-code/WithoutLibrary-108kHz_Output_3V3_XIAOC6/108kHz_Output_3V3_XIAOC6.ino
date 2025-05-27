/*
 * Mist Maker Controller for Xiao ESP32-C6
 * Features: 108.7kHz PWM, button control, current sensing
 */

// Pin Definitions
const int MIST_OUTPUT_PIN = D1;   // PWM output to mist maker
const int CURRENT_SENSE_PIN = D2; // Analog input for current sensing
const int EN_PIN = D3;            // Enable pin
const int BUTTON_PIN = D6;        // Button input
const int LED_PIN = D7;           // Built-in LED

// PWM Configuration
const int PWM_FREQ = 108700;  // 108.7kHz frequency
const int PWM_RESOLUTION = 8; // 8-bit resolution (0-255)
const int DUTY_CYCLE = 127;   // 50% duty cycle when ON

// State variables
bool mistState = false;          // Mist maker state (ON/OFF)
unsigned long startTime;         // System start time
unsigned long lastPrintTime = 0; // Last time we printed status

// Convert ADC reading to voltage (with 2x voltage divider compensation)
float getVoltage(int pin)
{
  return (analogRead(pin) / 4095.0) * 3.3 * 2.0;
}

// Turn mist maker on
void turnMistOn()
{
  digitalWrite(EN_PIN, HIGH);
  digitalWrite(LED_PIN, HIGH);
  ledcWrite(MIST_OUTPUT_PIN, DUTY_CYCLE);
  Serial.println("Mist maker ON - 50% power");
}

// Turn mist maker off
void turnMistOff()
{
  digitalWrite(EN_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  ledcWrite(MIST_OUTPUT_PIN, 0);
  digitalWrite(MIST_OUTPUT_PIN, LOW);
  Serial.println("Mist maker OFF");
}

// Print system status
void printStatus()
{
  float currentVoltage = getVoltage(CURRENT_SENSE_PIN);
  unsigned long runtime = (millis() - startTime) / 1000;

  Serial.print("Runtime: ");
  Serial.print(runtime);
  Serial.print(" seconds. Mist maker is ");
  Serial.print(mistState ? "ON" : "OFF");
  Serial.print(". Current sense: ");
  Serial.print(currentVoltage, 2);
  Serial.println(" V");

  lastPrintTime = millis();
}

void setup()
{
  // Initialize communication
  Serial.begin(115200);
  delay(1000);

  // Configure pins
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(EN_PIN, OUTPUT);
  pinMode(CURRENT_SENSE_PIN, INPUT);

  // Configure PWM for mist maker
  ledcAttach(MIST_OUTPUT_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(MIST_OUTPUT_PIN, 0);
  digitalWrite(EN_PIN, LOW);

  // Print startup information
  Serial.println("==============================================");
  Serial.println("Xiao ESP32-C6 Mist Maker Controller with Button");
  Serial.println("==============================================");
  Serial.println("Configuration:");
  Serial.print("Frequency: ");
  Serial.print(PWM_FREQ / 1000.0, 1);
  Serial.println(" kHz");
  Serial.println("PWM Resolution: 8-bit (0-255)");
  Serial.println("Mist Output Pin: D1");
  Serial.println("Current Sense Pin: D2");
  Serial.println("Button Pin: D6 (with pull-down)");
  Serial.println("LED Status: D7 (built-in)");
  Serial.println("==============================================");
  Serial.println("Press button to toggle mist maker on/off");

  // Initialize timers
  startTime = millis();
}

void loop()
{
  // Handle button press (toggle mist maker)
  if (digitalRead(BUTTON_PIN) == HIGH)
  {
    mistState = !mistState;

    if (mistState)
    {
      turnMistOn();
    }
    else
    {
      turnMistOff();
    }

    delay(300); // Debounce
  }

  // Periodic status update (every 5 seconds)
  if (millis() - lastPrintTime > 5000)
  {
    printStatus();
  }
}