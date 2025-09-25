#include <Arduino.h>
#include "pins.h" // Use existing pin definitions
#include <U8g2lib.h> // For OLED display

// --- OLED INSTANCE ---
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ I2C_SCL, /* data=*/ I2C_SDA
);

// --- UI Screens ---
enum class UiScreen : uint8_t {
  STATUS = 0,
  MENU
};

// --- State Enum (from state.h) ---
enum class SysState : uint8_t {
  UNHOMED = 0,
  HOMING_SEEK,
  HOMING_BACKOFF,
  HOMING_REAPP,
  READY,
  RUNNING,
  STOPPING,
  FAULT
};

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

// --- Homing Constants (from globals.cpp) ---
const float HOMING_V_SEEK_PPS    = 800.0f;
const float HOMING_V_REAPP_PPS   = 400.0f;
const float HOMING_BACKOFF_DEG   = 3.0f;
const uint32_t HOMING_TIMEOUT_STEPS = 5 * stepsPerRev; // Assuming 5 revolutions timeout

// --- Global Variables ---
long currentStep = 0;
unsigned long lastStepTime = 0;
bool motorDirectionCW = true; // true for CW (increasing steps), false for CCW (decreasing steps)
volatile float v_goal = 0.0f; // Target speed in steps/sec

volatile SysState state = SysState::UNHOMED;
volatile bool     homed = false;
volatile uint64_t homingStepCounter = 0;

UiScreen uiScreen = UiScreen::STATUS;
int menuIndex = 0; // 0 for HOME, 1 for RUN

// --- Button Debounce ---
const uint32_t DEBOUNCE_MS = 40;
uint32_t lastBtnHomeMs = 0;

// --- Encoder Variables ---
volatile int encoderPos = 0;
volatile int lastReportedPos = 0;
unsigned long lastEncoderReadTime = 0;
const unsigned long ENCODER_DEBOUNCE_MS = 50; // Debounce for encoder button

// --- Utility Functions ---
bool inRange(float x, float lo, float hi){ return (x >= lo) && (x < hi); }

float currentAngleDeg(long currentStepVal) {
  float degPerStepVal = 360.0f / (float)stepsPerRev;
  uint32_t modStepsVal = (uint32_t)(currentStepVal % stepsPerRev);
  return (float)modStepsVal * degPerStepVal;
}

// Adapted selectSectorProfile function
void selectSectorProfile(float deg) {
  if (inRange(deg, 0.0f, 5.0f) || // DEG_SLOW_Lo1, DEG_SLOW_Hi1
      inRange(deg, 355.0f, 360.0f) || // DEG_SLOW_Lo2, DEG_SLOW_Hi2
      inRange(deg, 175.0f, 185.0f)) { // DEG_TRANS_Lo, DEG_TRANS_Hi
    v_goal = SLOW_SPEED;
  } else if (inRange(deg, 5.0f, 175.0f)) { // DEG_MED_Lo, DEG_MED_Hi
    v_goal = MEDIUM_SPEED;
  } else if (inRange(deg, 185.0f, 355.0f)) { // DEG_FAST_Lo, DEG_FAST_Hi
    v_goal = FAST_SPEED;
  } else {
    v_goal = SLOW_SPEED; // Default to slow
  }
}

// --- Sensor Logic ---
// Assuming PIN_OPT_HOME is active LOW
bool optActive() {
  return digitalRead(PIN_OPT_HOME) == LOW;
}

// --- Button Logic ---
bool btnHomePhys(){ return digitalRead(PIN_BTN_HOME) == LOW; } // Assuming active LOW button

// --- Encoder ISRs ---
void IRAM_ATTR readEncoderA() {
  if (digitalRead(PIN_ENC_A) == digitalRead(PIN_ENC_B)) {
    encoderPos++;
  } else {
    encoderPos--;
  }
}

void IRAM_ATTR readEncoderB() {
  if (digitalRead(PIN_ENC_A) != digitalRead(PIN_ENC_B)) {
    encoderPos++;
  } else {
    encoderPos--;
  }
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
    homingStepCounter++; // Increment homing step counter with each step
  }
}

void setZeroHere(){ currentStep = 0; homed = true; }

void startHoming(){
  homed = false;
  homingStepCounter = 0;
  state = SysState::HOMING_SEEK;
  Serial.println("[HOME] Iniciando homing...");
}

const char* stateName(SysState s) {
  switch (s) {
    case SysState::UNHOMED:        return "UNHOMED";
    case SysState::HOMING_SEEK:    return "HOMING SEEK";
    case SysState::HOMING_BACKOFF: return "HOMING BACK";
    case SysState::HOMING_REAPP:   return "HOMING REAPP";
    case SysState::READY:          return "READY";
    case SysState::RUNNING:        return "RUNNING";
    case SysState::STOPPING:       return "STOPPING";
    case SysState::FAULT:          return "FAULT";
  }
  return "?";
}

// --- UI Drawing Functions ---
static void drawStatusScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Status");

  char line[40];
  snprintf(line, sizeof(line), "State: %s", stateName(state));
  u8g2.drawStr(0, 22, line);
  snprintf(line, sizeof(line), "Homed: %s", homed ? "YES" : "NO");
  u8g2.drawStr(0, 34, line);
  snprintf(line, sizeof(line), "Step: %ld", currentStep);
  u8g2.drawStr(0, 46, line);
  snprintf(line, sizeof(line), "Speed: %.0f pps", v_goal);
  u8g2.drawStr(0, 58, line);

  u8g2.sendBuffer();
}

static void drawMenuScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "MENU");

  const char* menuItems[] = {"HOME", "RUN"};
  const int numItems = sizeof(menuItems) / sizeof(menuItems[0]);

  for (int i = 0; i < numItems; ++i) {
    uint8_t y = 22 + i * 12;
    if (i == menuIndex) {
      u8g2.drawBox(0, y - 10, 128, 12);
      u8g2.setDrawColor(0);
      u8g2.drawStr(2, y, menuItems[i]);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(2, y, menuItems[i]);
    }
  }
  u8g2.sendBuffer();
}

// --- UI Process and Render ---
void uiProcess(int8_t encDelta, bool encClick) {
  switch (uiScreen) {
    case UiScreen::STATUS:
      if (encClick) {
        uiScreen = UiScreen::MENU;
      }
      break;
    case UiScreen::MENU:
      if (encDelta != 0) {
        menuIndex = constrain(menuIndex + encDelta, 0, 1); // 0 for HOME, 1 for RUN
      }
      if (encClick) {
        if (menuIndex == 0) { // HOME selected
          if (state != SysState::RUNNING) {
            startHoming();
            uiScreen = UiScreen::STATUS;
          }
        } else if (menuIndex == 1) { // RUN selected
          if (homed && state == SysState::READY) {
            state = SysState::RUNNING;
            uiScreen = UiScreen::STATUS;
            Serial.println("[UI] RUNNING iniciado.");
          }
        }
      }
      break;
  }
}

void uiRender() {
  if (uiScreen == UiScreen::STATUS) {
    drawStatusScreen();
  } else if (uiScreen == UiScreen::MENU) {
    drawMenuScreen();
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

  // Configure optical sensor pin
  pinMode(PIN_OPT_HOME, INPUT);

  // Configure button pin
  pinMode(PIN_BTN_HOME, INPUT_PULLUP);

  // Configure encoder pins
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_BTN, INPUT_PULLUP);

  // Attach encoder ISRs
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), readEncoderA, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), readEncoderB, CHANGE);

  // OLED Init
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);

  state = SysState::UNHOMED;
  homed = false;
  setMotorDirection(true); // Default direction for homing/running

  Serial.println("--- Stepper Test with Home/Run Logic and UI ---");
  Serial.printf("Initial State: %s, Homed: %d\n", stateName(state), homed);
}

void loop() {
  uint32_t now = millis();

  // Read encoder delta and click
  int8_t encDelta = 0;
  bool encClick = false;

  if (encoderPos != lastReportedPos) {
    encDelta = encoderPos - lastReportedPos;
    lastReportedPos = encoderPos;
  }

  // Simple encoder button debounce
  if (digitalRead(PIN_ENC_BTN) == LOW && (now - lastEncoderReadTime > ENCODER_DEBOUNCE_MS)) {
    encClick = true;
    lastEncoderReadTime = now;
  }

  // Process UI input
  uiProcess(encDelta, encClick);

  // Handle physical HOME button press (can also be used as START/STOP)
  if (btnHomePhys() && (now - lastBtnHomeMs > DEBOUNCE_MS)) {
    lastBtnHomeMs = now;
    if (state == SysState::UNHOMED || state == SysState::READY) {
      startHoming();
    } else if (state == SysState::RUNNING) {
      // Stop running, go to READY
      state = SysState::READY;
      Serial.println("[RUN] Stopped. READY.");
    } else if (state == SysState::HOMING_SEEK || state == SysState::HOMING_BACKOFF || state == SysState::HOMING_REAPP) {
      // During homing, button press can stop it and go to UNHOMED
      state = SysState::UNHOMED;
      homed = false;
      Serial.println("[HOME] Homing interrupted. UNHOMED.");
    }
  }

  switch (state) {
    case SysState::UNHOMED:
      // Wait for button press to start homing or menu interaction
      v_goal = 0; // Motor stopped
      break;

    case SysState::HOMING_SEEK:
      v_goal = HOMING_V_SEEK_PPS;
      setMotorDirection(true); // Seek in CW direction
      stepMotor((int)v_goal);
      if (optActive()) {
        homingStepCounter = 0; // Reset counter for backoff
        state = SysState::HOMING_BACKOFF;
        Serial.println("[HOME] Sensor detected. Backoff...");
      } else if (homingStepCounter > HOMING_TIMEOUT_STEPS) {
        state = SysState::FAULT;
        Serial.println("[HOME] TIMEOUT SEEK.");
      }
      break;

    case SysState::HOMING_BACKOFF: {
      v_goal = HOMING_V_SEEK_PPS;
      setMotorDirection(false); // Backoff in CCW direction
      stepMotor((int)v_goal);
      uint32_t backoffSteps = (uint32_t)(HOMING_BACKOFF_DEG / (360.0f / (float)stepsPerRev));
      if (homingStepCounter >= backoffSteps) {
        homingStepCounter = 0; // Reset counter for re-approach
        state = SysState::HOMING_REAPP;
        Serial.println("[HOME] Backoff OK. Re-approach slow...");
      }
    } break;

    case SysState::HOMING_REAPP:
      v_goal = HOMING_V_REAPP_PPS;
      setMotorDirection(true); // Re-approach in CW direction
      stepMotor((int)v_goal);
      if (optActive()) {
        setZeroHere();
        state = SysState::READY;
        Serial.println("[HOME] CERO fixed. READY.");
      } else if (homingStepCounter > HOMING_TIMEOUT_STEPS) {
        state = SysState::FAULT;
        Serial.println("[HOME] TIMEOUT REAPP.");
      }
      break;

    case SysState::READY:
      // Motor stopped, waiting for button press to RUN or HOME again
      v_goal = 0;
      break;

    case SysState::RUNNING:
      // Use the three-speed logic
      setMotorDirection(true); // Always run CW in this test
      float currentDeg = currentAngleDeg(currentStep);
      selectSectorProfile(currentDeg);
      stepMotor((int)v_goal);
      break;

    case SysState::STOPPING:
      // Not implemented for this test, directly go to READY for simplicity
      state = SysState::READY;
      break;

    case SysState::FAULT:
      // Motor stopped, indicate fault
      v_goal = 0;
      break;
  }

  // Render UI
  uiRender();

  // --- Serial Telemetry ---
  static unsigned long lastTelemetryTime = 0;
  if (millis() - lastTelemetryTime > 1000) {
    lastTelemetryTime = millis();
    Serial.printf("State: %s | Homed: %d | Current Step: %ld | Current Deg: %.2f | Speed: %.0f pps | Sensor: %d\n",
                  stateName(state), homed ? 1 : 0, currentStep, currentAngleDeg(currentStep), v_goal, optActive());
  }
}
