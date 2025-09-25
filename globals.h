#pragma once
#include <Arduino.h>
#include "state.h"

namespace App {

// ===== Config persistente (EEPROM) =====
struct Config {
  uint32_t magic;          // 'OLE3'
  float cm_per_rev;        // cm por vuelta
  float v_slow_cmps;       // cm/s
  float v_med_cmps;        // cm/s
  float v_fast_cmps;       // cm/s
  float accel_cmps2;       // cm/s^2
  float jerk_cmps3;        // cm/s^3
  bool enable_s_curve;     // true=S-curve ON, false=control directo
  uint32_t crc;
};
extern Config Cfg;

// ===== Parámetros mecánicos =====
extern uint32_t MOTOR_FULL_STEPS_PER_REV; // 200
extern uint32_t MICROSTEPPING;            // 16
extern float    GEAR_RATIO;               // 1.0

// ===== Estados =====
extern volatile SysState state;
extern volatile bool     homed;
extern RunMode RUN_MODE;

// ===== Timer de control =====
extern esp_timer_handle_t controlTimer;

// ===== Cinemática en pasos/seg =====
extern volatile uint64_t totalSteps;
extern volatile uint32_t stepsPerRev;

extern volatile float v;         // pps
extern volatile float a;         // pps^2
extern volatile float v_goal;    // pps objetivo
extern volatile float A_MAX;     // pps^2
extern volatile float J_MAX;     // pps^3

extern volatile bool  stepPinState;
extern volatile uint32_t pulseHoldUs;
extern volatile float    stepAccumulatorUs;

extern volatile bool  dirCW;
extern volatile uint64_t homingStepCounter;

extern uint32_t revStartModSteps;

// Variables para comando ROTAR
extern volatile bool rotateMode;           // true cuando está en modo rotación
extern volatile float rotateTargetRevs;    // número de vueltas objetivo (puede ser negativo)
extern volatile float rotateCurrentRevs;   // vueltas completadas
extern volatile bool rotateDirection;      // true=CW, false=CCW
extern volatile int32_t rotateStepsCounter; // contador independiente de pasos
extern volatile int32_t rotateTargetSteps;  // pasos objetivo total

// Debounce y parpadeo LED
extern const uint32_t DEBOUNCE_MS;
extern uint32_t lastBtnHomeMs;
extern uint32_t lastBtnStartMs;

extern bool     ledVerdeBlinkState;
extern uint32_t ledBlinkLastMs;

// Sectores (grados) - Rangos simples
struct SectorRange {
  float start;    // grado inicial 
  float end;      // grado final
  bool wraps;     // true si cruza 360->0 (ej: 350-10)
};

extern SectorRange DEG_LENTO;     // Sector lento (tomar/soltar huevo)
extern SectorRange DEG_MEDIO;     // Sector medio (transporte)
extern SectorRange DEG_RAPIDO;    // Sector rápido (retorno vacío)

// Homing pps
extern bool  HOMING_SEEK_DIR_CW;
extern float HOMING_V_SEEK_PPS;
extern float HOMING_A_SEEK_PPS2;
extern float HOMING_J_SEEK_PPS3;
extern float HOMING_V_REAPP_PPS;
extern float HOMING_A_REAPP_PPS2;
extern float HOMING_J_REAPP_PPS3;
extern float HOMING_BACKOFF_DEG;
extern uint32_t HOMING_TIMEOUT_STEPS;

// Homing avanzado (nuevos globals)
extern uint32_t TIEMPO_ESTABILIZACION_HOME; // ms, default 2000
extern float    DEG_OFFSET;                 // degrees, default 45.0
extern uint32_t MAX_STEPS_TO_FIND_SENSOR;   // steps, default 4800

// UI
extern UiScreen uiScreen;
extern UiScreen previousUiScreen; // Para volver de dialogos
extern int menuIndex;
extern int editIndex;
extern int confirmIndex; // 0=No, 1=Si
extern uint8_t menuOrder[MI_COUNT];

// Constantes control
extern const bool     DIR_CW_LEVEL;
extern const uint32_t STEP_PULSE_WIDTH_US;
extern const uint32_t CONTROL_DT_US;

// Utils acceso rápido
float degPerStep();
uint32_t modSteps();
float currentAngleDeg();
bool inRange(float x, float lo, float hi);
bool inSectorRange(float deg, const SectorRange& sector);
void setDirection(bool cw);
bool optActive();
bool btnHomePhys();
bool btnStartPhys();

const char* stateName(SysState s);
const char* sectorName(float deg);
bool isMotorMoving(); // Helper for UI

// helpers homing
void setZeroHere();
void startHoming();
void forceStopTarget();
bool crossedZeroThisTick(uint32_t prevMod, uint32_t currMod);

// helpers rotación
void startRotation(float revs);
void stopRotation();

} // namespace App
