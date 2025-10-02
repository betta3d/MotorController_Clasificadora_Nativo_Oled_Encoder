
#include "globals.h"
#include "pins.h"
#include <Arduino.h>

namespace App {

Config Cfg;

// Parámetros homing adaptativo (se inicializan con defaults, luego loadConfig puede sobrescribir)
float HOMING_SWITCH_TURNS = 0.70f;
float HOMING_TIMEOUT_TURNS = 1.40f;
volatile uint32_t homingFaultCount = 0;

uint32_t MOTOR_FULL_STEPS_PER_REV = 200;
uint32_t MICROSTEPPING             = 16;
float    GEAR_RATIO                = 1.0f;

volatile SysState state = SysState::UNHOMED;
volatile bool     homed = false;
RunMode RUN_MODE = RunMode::CONTINUOUS;

// Rotación pendiente
float pendingRotateRevs = 0.0f;

esp_timer_handle_t controlTimer = nullptr;

volatile int64_t  totalSteps = 0; // signed para permitir decrementos seguros
volatile uint32_t stepsPerRev = 200 * 16;

volatile float v = 0.0f;
volatile float a = 0.0f;
volatile float v_goal = 0.0f;
volatile float A_MAX  = 0.0f;
volatile float J_MAX  = 0.0f;

volatile bool  stepPinState = LOW;
volatile uint32_t pulseHoldUs  = 0;
volatile float    stepAccumulatorUs = 0.0f;

volatile bool  master_direction = true;  // CW por defecto (HIGH)
volatile bool  inverse_direction = !master_direction; // CCW por defecto (LOW) - opuesto a master
volatile bool  currentDirIsMaster = true; // selector actualmente aplicado
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

// Sectores - 4 segmentos sobre 360° con 3 velocidades
SectorRange DEG_LENTO_UP   = {350.0f, 10.0f, true};   // 350°-10° (wrap) — tomar huevo (Lento)
SectorRange DEG_MEDIO      = {10.0f, 170.0f, false};  // 10°-170° — transporte (Medio)
SectorRange DEG_LENTO_DOWN = {170.0f, 190.0f, false}; // 170°-190° — dejar huevo (Lento)
SectorRange DEG_TRAVEL     = {190.0f, 350.0f, false}; // 190°-350° — retorno (Rápido)

// Homing centralizado
uint32_t TIEMPO_ESTABILIZACION_HOME = 2000; // 2 segundos
float  DEG_OFFSET = -5.0f;                // -5 grados
// float V_HOME_CMPS = 3.0f; // cm/s - MOVIDO A homing.cpp

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
const uint32_t STEP_PULSE_WIDTH_US = 20;
const uint32_t CONTROL_DT_US       = 1000;

float degPerStep() { return 360.0f / (float)stepsPerRev; }
uint32_t modSteps(){
  int64_t m = totalSteps % (int64_t)stepsPerRev;
  if (m < 0) m += stepsPerRev; // normalizar a rango positivo
  return (uint32_t)m;
}
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
void setDirection(bool useMasterDir){
  currentDirIsMaster = useMasterDir;
  bool physical = useMasterDir ? master_direction : inverse_direction;
  digitalWrite(PIN_DIR, physical ? HIGH : LOW);
}
bool optActive(){ return digitalRead(PIN_OPT_HOME) == HIGH; }
bool btnHomePhys(){ return digitalRead(PIN_BTN_HOME) == LOW; }
bool btnStartPhys(){ return digitalRead(PIN_BTN_START) == LOW; }

const char* stateName(SysState s) {
  switch (s) {
    case SysState::UNHOMED:        return "UNHOMED";
    case SysState::HOMING_SEEK:    return "HOMING SEEK";
    case SysState::READY:          return "READY";
    case SysState::RUNNING:        return "RUNNING";
    case SysState::ROTATING:       return "ROTATING";
    case SysState::STOPPING:       return "STOPPING";
    case SysState::FAULT:          return "FAULT";
  }
  return "?";
}
const char* sectorName(float deg) {
  if (inSectorRange(deg, DEG_LENTO_UP))   return "LENTO_UP";
  if (inSectorRange(deg, DEG_MEDIO))      return "MEDIO";  
  if (inSectorRange(deg, DEG_LENTO_DOWN)) return "LENTO_DOWN";
  if (inSectorRange(deg, DEG_TRAVEL))     return "TRAVEL";
  return "-";
}

bool isMotorMoving() {
  return (state == SysState::RUNNING ||
          state == SysState::ROTATING ||
          state == SysState::HOMING_SEEK);
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
