#include <Arduino.h>
#include "pins.h" // Use existing pin definitions

// --- Motor Parameters ---
const uint32_t MOTOR_FULL_STEPS_PER_REV = 200;
const uint32_t MICROSTEPPING = 16;
const float    GEAR_RATIO = 1.0f;
const uint32_t stepsPerRev = (uint32_t)(MOTOR_FULL_STEPS_PER_REV * MICROSTEPPING * GEAR_RATIO);
const long MAX_STEPS = 20000; // Total travel for the motor (approx 55.5 revolutions)

// --- Motor Speeds (steps/sec) ---
const int SLOW_SPEED = 300;
const int MEDIUM_SPEED = 1500;
const int FAST_SPEED = 6000;

// --- Global Variables ---
long currentStep = 0;
unsigned long lastStepTime = 0;
bool motorDirectionCW = true; // true for CW (increasing steps), false for CCW (decreasing steps)
volatile float v_goal = 0.0f; // Target speed in steps/sec

// --- Sector Definitions (Degrees) ---
// These define the angular ranges for different speeds, adapted from globals.cpp
const float DEG_SLOW_Lo1 =   0.0f, DEG_SLOW_Hi1 =   5.0f;
const float DEG_SLOW_Lo2 = 355.0f, DEG_SLOW_Hi2 = 360.0f;
const float DEG_MED_Lo   =   5.0f, DEG_MED_Hi   = 175.0f;
const float DEG_TRANS_Lo = 175.0f, DEG_TRANS_Hi = 185.0f; // Transition zone, treated as slow
const float DEG_FAST_Lo  = 185.0f, DEG_FAST_Hi  = 355.0f;

// --- Utility Functions ---
bool inRange(float x, float lo, float hi){ return (x >= lo) && (x < hi); }

float currentAngleDeg(long currentStepVal) {
  float degPerStepVal = 360.0f / (float)stepsPerRev;
  uint32_t modStepsVal = (uint32_t)(currentStepVal % stepsPerRev);
  return (float)modStepsVal * degPerStepVal;
}

// Adapted selectSectorProfile function
void selectSectorProfile(float deg) {
  if (inRange(deg, DEG_SLOW_Lo1, DEG_SLOW_Hi1) ||
      inRange(deg, DEG_SLOW_Lo2, DEG_SLOW_Hi2) ||
      inRange(deg, DEG_TRANS_Lo, DEG_TRANS_Hi)) {
    v_goal = SLOW_SPEED;
  } else if (inRange(deg, DEG_MED_Lo, DEG_MED_Hi)) {
    v_goal = MEDIUM_SPEED;
  } else if (inRange(deg, DEG_FAST_Lo, DEG_FAST_Hi)) {
    v_goal = FAST_SPEED;
  } else {
    v_goal = SLOW_SPEED; // Default to slow
  }
}

// --- Sensor Logic (from globals.cpp) ---
// Assuming PIN_OPT_HOME is active HIGH (2.4V when active)
bool optActive() {
  return digitalRead(PIN_OPT_HOME) == HIGH;
}

// --- Motor Control Functions ---
void setMotorDirection(bool cw) {
  digitalWrite(PIN_DIR, cw ? HIGH : LOW); // Assuming HIGH for CW, LOW for CCW
  motorDirectionCW = cw;
}

void stepMotor(int speed_pps) {
  if (speed_pps <= 0) return;

  unsigned long stepIntervalUs = 1000000 / speed_pps; // Microseconds per step

  if (micros() - lastStepTime >= stepIntervalUs) {
    digitalWrite(PIN_STEP, HIGH);
    delayMicroseconds(100); // Pulse width
    digitalWrite(PIN_STEP, LOW);
    lastStepTime = micros();

    if (motorDirectionCW) {
      currentStep++;
    } else {
      currentStep--;
    }
  }
}

// --- Main Setup and Loop ---
void setup() {
  Serial.begin(115200);
  delay(100); // Give serial time to init

  // Configure motor pins
  pinMode(PIN_STEP, OUTPUT);
  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_ENA, OUTPUT);
  digitalWrite(PIN_ENA, LOW); // Enable driver

  // Configure optical sensor pin (not used in this simplified test, but good practice)
  pinMode(PIN_OPT_HOME, INPUT);

  Serial.println("--- Variable Speed Stepper Motor Test ---");
  setMotorDirection(true); // Start CW
}

void loop() {
  // Calculate current angle and select speed profile
  float currentDeg = currentAngleDeg(currentStep);
  selectSectorProfile(currentDeg);

  // Step the motor with the determined speed
  stepMotor((int)v_goal);

  // --- Serial Telemetry (Optional) ---
  static unsigned long lastTelemetryTime = 0;
  if (millis() - lastTelemetryTime > 1000) {
    lastTelemetryTime = millis();
    Serial.printf("Current Step: %ld | Current Deg: %.2f | Speed: %.0f pps | Sensor: %d\n",
                  currentStep, currentDeg, v_goal, optActive());
  }
}