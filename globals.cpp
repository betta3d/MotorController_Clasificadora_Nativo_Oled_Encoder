#include "globals.h"
#include "pins.h"
#include <Arduino.h>

namespace App {

Config Cfg;

uint32_t MOTOR_FULL_STEPS_PER_REV = 200;
uint32_t MICROSTEPPING             = 16;
float    GEAR_RATIO                = 1.0f;

volatile SysState state = SysState::UNHOMED;
volatile bool     homed = false;
RunMode RUN_MODE = RunMode::CONTINUOUS;

esp_timer_handle_t controlTimer = nullptr;

volatile uint64_t totalSteps = 0;
volatile uint32_t stepsPerRev = 200 * 16;

volatile float v = 0.0f;
volatile float a = 0.0f;
volatile float v_goal = 0.0f;
volatile float A_MAX  = 0.0f;
volatile float J_MAX  = 0.0f;

volatile bool  stepPinState = LOW;
volatile uint32_t pulseHoldUs  = 0;
volatile float    stepAccumulatorUs = 0.0f;

volatile bool  dirCW = true;
volatile uint64_t homingStepCounter = 0;

uint32_t revStartModSteps = 0;

// Variables para comando ROTAR
volatile bool rotateMode = false;
volatile float rotateTargetRevs = 0.0f;
volatile float rotateCurrentRevs = 0.0f;
volatile bool rotateDirection = true;
volatile int32_t rotateStepsCounter = 0;
volatile int32_t rotateTargetSteps = 0;

const uint32_t DEBOUNCE_MS = 40;
uint32_t lastBtnHomeMs  = 0;
uint32_t lastBtnStartMs = 0;

bool     ledVerdeBlinkState = false;
uint32_t ledBlinkLastMs     = 0;

// Sectores - Estructura simplificada
SectorRange DEG_LENTO  = {355.0f, 10.0f, true};   // 355°-10° (tomar/soltar huevo)
SectorRange DEG_MEDIO  = {10.0f, 180.0f, false};  // 10°-180° (transporte)
SectorRange DEG_RAPIDO = {180.0f, 355.0f, false}; // 180°-355° (retorno vacío)

// Homing
bool  HOMING_SEEK_DIR_CW   = true;
float HOMING_V_SEEK_PPS    = 800.0f;
float HOMING_A_SEEK_PPS2   = 3000.0f;
float HOMING_J_SEEK_PPS3   = 20000.0f;
float HOMING_V_REAPP_PPS   = 400.0f;
float HOMING_A_REAPP_PPS2  = 2000.0f;
float HOMING_J_REAPP_PPS3  = 15000.0f;
float HOMING_BACKOFF_DEG   = 3.0f;
uint32_t HOMING_TIMEOUT_STEPS = 0;

// Homing avanzado defaults
uint32_t TIEMPO_ESTABILIZACION_HOME = 2000; // 2 segundos
float    DEG_OFFSET = 45.0f;                // 45 grados
uint32_t MAX_STEPS_TO_FIND_SENSOR = 4800;   // 4800 pasos

// UI
UiScreen uiScreen = UiScreen::STATUS;
UiScreen previousUiScreen = UiScreen::STATUS;
int menuIndex = 0;
int editIndex = 0;
int confirmIndex = 0;
uint8_t menuOrder[MI_COUNT] = {
  MI_START, MI_HOME,
  MI_EDIT_VSLOW, MI_EDIT_VMED, MI_EDIT_VFAST,
  MI_EDIT_ACCEL, MI_EDIT_JERK, MI_EDIT_CMPERREV,
  MI_SAVE, MI_DEFAULTS, MI_BACK
};

// Constantes control
const bool     DIR_CW_LEVEL        = HIGH;
const uint32_t STEP_PULSE_WIDTH_US = 20;
const uint32_t CONTROL_DT_US       = 1000;

float degPerStep() { return 360.0f / (float)stepsPerRev; }
uint32_t modSteps(){ return (uint32_t)(totalSteps % stepsPerRev); }
float currentAngleDeg(){ return modSteps() * degPerStep(); }
bool inRange(float x, float lo, float hi){ return (x >= lo) && (x < hi); }

// Helper para verificar si un ángulo está dentro de un sector
bool inSectorRange(float deg, const SectorRange& sector) {
  // Normalizar ángulo a 0-360
  while (deg < 0) deg += 360.0f;
  while (deg >= 360.0f) deg -= 360.0f;
  
  if (sector.wraps) {
    // El sector cruza la línea 360°->0° (ej: 355°-10°)
    return (deg >= sector.start || deg <= sector.end);
  } else {
    // Sector normal (ej: 10°-180°)
    return (deg >= sector.start && deg <= sector.end);
  }
}
void setDirection(bool cw){ digitalWrite(PIN_DIR, cw ? DIR_CW_LEVEL : !DIR_CW_LEVEL); dirCW = cw; }
bool optActive(){ return digitalRead(PIN_OPT_HOME) == HIGH; }
bool btnHomePhys(){ return digitalRead(PIN_BTN_HOME) == LOW; }
bool btnStartPhys(){ return digitalRead(PIN_BTN_START) == LOW; }

const char* stateName(SysState s) {
  switch (s) {
    case SysState::UNHOMED:        return "UNHOMED";
    case SysState::HOMING_SEEK:    return "HOMING SEEK";
    case SysState::HOMING_BACKOFF: return "HOMING BACK";
    case SysState::HOMING_REAPP:   return "HOMING REAPP";
    case SysState::READY:          return "READY";
    case SysState::RUNNING:        return "RUNNING";
    case SysState::ROTATING:       return "ROTATING";
    case SysState::STOPPING:       return "STOPPING";
    case SysState::FAULT:          return "FAULT";
  }
  return "?";
}
const char* sectorName(float deg) {
  if (inSectorRange(deg, DEG_LENTO))  return "LENTO";
  if (inSectorRange(deg, DEG_MEDIO))  return "MEDIO";  
  if (inSectorRange(deg, DEG_RAPIDO)) return "RAPIDO";
  return "-";
}

bool isMotorMoving() {
  return (state == SysState::RUNNING ||
          state == SysState::ROTATING ||
          state == SysState::HOMING_SEEK ||
          state == SysState::HOMING_BACKOFF ||
          state == SysState::HOMING_REAPP);
}

void setZeroHere(){ totalSteps = 0; homed = true; }
void startHoming(){
  homed = false;
  homingStepCounter = 0;
  v = 0.0f; // Reset velocity
  a = 0.0f; // Reset acceleration
  state = SysState::HOMING_SEEK;
}
void forceStopTarget(){ v_goal = 0.0f; A_MAX = 0.0f; J_MAX = 0.0f; }
bool crossedZeroThisTick(uint32_t prevMod, uint32_t currMod){ return (currMod < prevMod); }

void startRotation(float revs) {
  if (state == SysState::READY) {
    rotateTargetRevs = revs;
    rotateCurrentRevs = 0.0f;
    rotateDirection = (revs > 0);
    rotateMode = true;
  }
}

void stopRotation() {
  rotateMode = false;
  forceStopTarget();
}

} // namespace App
