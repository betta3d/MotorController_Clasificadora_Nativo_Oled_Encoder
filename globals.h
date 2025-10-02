#pragma once
#include <Arduino.h>
#include "state.h"

extern float V_HOME_CMPS; // Velocidad de homing en cm/s

namespace App {

// Sectores (grados) - Rangos simples
struct SectorRange {
  float start;    // grado inicial 
  float end;      // grado final
  bool wraps;     // true si cruza 360->0 (ej: 350-10)
};

// ===== Config persistente (EEPROM) =====
struct Config {
  uint32_t magic;          // 'OLE3'
  // Cinemática en cm
  float cm_per_rev;        // cm por vuelta
  float v_slow_cmps;       // cm/s
  float v_med_cmps;        // cm/s
  float v_fast_cmps;       // cm/s
  float accel_cmps2;       // cm/s^2
  float jerk_cmps3;        // cm/s^3
  bool  enable_s_curve;    // true=S-curve ON, false=control directo

  // Homing
  float    v_home_cmps;    // cm/s
  uint32_t t_estab_ms;     // ms
  float    deg_offset;     // grados
  // Homing adaptativo (nuevo)
  float    homing_switch_turns;   // vueltas locales antes de invertir dirección (ej 0.7)
  float    homing_timeout_turns;  // vueltas totales antes de fault (ej 1.4)

  // Mecánica
  uint32_t motor_full_steps_per_rev; // p.ej. 200
  uint32_t microstepping;            // p.ej. 16
  float    gear_ratio;               // p.ej. 1.0

  // Dirección
  bool     master_dir_cw; // true=CW, false=CCW

  // Sectores
  SectorRange cfg_deg_lento_up;
  SectorRange cfg_deg_medio;
  SectorRange cfg_deg_lento_down;
  SectorRange cfg_deg_travel;

  // WiFi credenciales guardadas (simple)
  char wifi_ssid[32];
  char wifi_pass[32];
  uint8_t wifi_valid; // 1 si credenciales válidas

  uint32_t crc;
};
extern Config Cfg;

// Variables runtime accesibles (alias legibles)
extern float HOMING_SWITCH_TURNS;   // default 0.70
extern float HOMING_TIMEOUT_TURNS;  // default 1.40
extern volatile uint32_t homingFaultCount; // contador no persistente de fallas homing

// ===== Parámetros mecánicos =====
extern uint32_t MOTOR_FULL_STEPS_PER_REV; // 200
extern uint32_t MICROSTEPPING;            // 16
extern float    GEAR_RATIO;               // 1.0

// ===== Estados =====
extern volatile SysState state;
extern volatile bool     homed;
extern RunMode RUN_MODE;

// ===== Rotación pendiente =====
extern float pendingRotateRevs; // 0 = no hay rotación pendiente

// ===== Timer de control =====
extern esp_timer_handle_t controlTimer;

// ===== Cinemática en pasos/seg =====
extern volatile int64_t  totalSteps; // signed to allow decrement without underflow
extern volatile uint32_t stepsPerRev;

extern volatile float v;         // pps
extern volatile float a;         // pps^2
extern volatile float v_goal;    // pps objetivo
extern volatile float A_MAX;     // pps^2
extern volatile float J_MAX;     // pps^3

extern volatile bool  stepPinState;
extern volatile uint32_t pulseHoldUs;
extern volatile float    stepAccumulatorUs;

extern volatile bool  master_direction; // true=CW, false=CCW
extern volatile bool  inverse_direction; // negado de master_direction
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

// 4 sectores que cubren 360° pero con 3 velocidades (Lento/Medio/Rápido)
extern SectorRange DEG_LENTO_UP;    // 350°-10° (wrap) — tomar huevo (Lento)
extern SectorRange DEG_MEDIO;       // 10°-170° — transporte (Medio)
extern SectorRange DEG_LENTO_DOWN;  // 170°-190° — dejar huevo (Lento)
extern SectorRange DEG_TRAVEL;      // 190°-350° — retorno sin carga (Rápido)

// Homing centralizado
extern uint32_t TIEMPO_ESTABILIZACION_HOME; // ms, default 2000
extern float    DEG_OFFSET;                 // degrees, default 45.0

// UI
extern UiScreen uiScreen;
extern UiScreen previousUiScreen; // Para volver de dialogos
extern int menuIndex;
extern int editIndex;
extern int confirmIndex; // 0=No, 1=Si
extern uint8_t menuOrder[MI_COUNT];

// Constantes control
extern const uint32_t STEP_PULSE_WIDTH_US;
extern const uint32_t CONTROL_DT_US;

// Utils acceso rápido
float degPerStep();
uint32_t modSteps();
float currentAngleDeg();
bool inRange(float x, float lo, float hi);
bool inSectorRange(float deg, const SectorRange& sector);
void setDirection(bool useMasterDir);
// Último selector aplicado (true=usando master_direction, false=usando inverse_direction)
extern volatile bool currentDirIsMaster;
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
