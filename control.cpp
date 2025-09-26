#include "control.h"
#include "globals.h"
#include "motion.h"
#include "pins.h"
#include "logger.h"
#include <Arduino.h>
#include <esp_timer.h>

namespace App {

// Nuevo: temporizadores dedicados para generar STEP con alta resolución
static esp_timer_handle_t stepTimer = nullptr;     // Programa el próximo flanco de subida (paso)
static esp_timer_handle_t stepOffTimer = nullptr;  // Apaga el pulso tras STEP_PULSE_WIDTH_US

// Adelantos de funciones
static void IRAM_ATTR stepOnTick(void* /*arg*/);
static void IRAM_ATTR stepOffTick(void* /*arg*/);

// Función encapsulada para movimiento por sectores
static void applySectorBasedMovement(bool useMasterSelector) {
  float deg = currentAngleDeg();
  selectSectorProfile(deg);
  
  // Usar dirección específica recibida como parámetro
  setDirection(useMasterSelector);
}

static void IRAM_ATTR controlTick(void* /*arg*/) {
  const float dt    = (float)CONTROL_DT_US * 1e-6f;
  const float dt_us = (float)CONTROL_DT_US;

  switch (state) {
    case SysState::RUNNING: {
      // Movimiento continuo por sectores hasta STOP o FAULT
  applySectorBasedMovement(true);
    } break;

    case SysState::ROTATING: {
      // Movimiento por N vueltas específicas con misma lógica de sectores
      // Usa la dirección según rotateDirection (true=positivo, false=negativo)
  bool useMasterSel = rotateDirection ? true : false;
  applySectorBasedMovement(useMasterSel);
      
      // DEBUG: Mostrar estado cada 100 ticks (0.1 segundos)
      static uint32_t debugCounter = 0;
      debugCounter++;
      if (debugCounter % 100 == 0) { // Cada 100ms
        logPrintf("DEBUG", "ROTATING: deg=%.1f, v_goal=%.1f, A_MAX=%.1f, sector=%s", 
                 currentAngleDeg(), v_goal, A_MAX, sectorName(currentAngleDeg()));
      }
    } break;

    case SysState::HOMING_SEEK: {
      // Estado de homing: la dirección y v_goal son manejados por homing.cpp
      // No necesitan configuración adicional aquí, solo permitir el movimiento
      break;
    }

    case SysState::FAULT:
      // Parada de emergencia: resetea inmediatamente la cinemática.
      v = 0.0f;
      a = 0.0f;
      forceStopTarget();
      break;

    default:
      forceStopTarget();
      break;
  }

  // S-curve jerk-limited (condicional según configuración)
  // IMPORTANTE: Durante HOMING_SEEK forzamos control directo para evitar A_MAX/J_MAX=0
  if (Cfg.enable_s_curve && state != SysState::HOMING_SEEK) {
    // S-curve habilitada (para RUNNING y ROTATING)
    float sign   = (v < v_goal) ? +1.0f : (v > v_goal ? -1.0f : 0.0f);
    float a_goal = sign * A_MAX;
    float da     = a_goal - a;
    float max_da = J_MAX * dt;
    if (da >  max_da) da =  max_da;
    if (da < -max_da) da = -max_da;
    a += da;

    v += a * dt;
    if (v < 0.0f) v = 0.0f;
  } else {
    // Control directo sin S-curve (homing y cuando S-curve está OFF)
    v = v_goal;
    a = 0.0f;
  }
  // Pulsos STEP ahora generados por stepTimer (alta resolución)

  // Modo ROTATING: verificar si se completaron los pasos objetivo
  if (state == SysState::ROTATING) {
    bool completed = false;
    int32_t completedSteps = 0;
    
    if (rotateDirection && rotateStepsCounter >= rotateTargetSteps) {
      completed = true;
      completedSteps = rotateStepsCounter;
    } else if (!rotateDirection && abs(rotateStepsCounter) >= rotateTargetSteps) {
      completed = true;
      completedSteps = abs(rotateStepsCounter);
    }
    
    // DEBUG: Mostrar progreso cada 10 grados
    static float lastDebugAngle = -1.0f;
    float currentAngle = currentAngleDeg();
    float totalAngleRotated = (float)abs(rotateStepsCounter) * degPerStep();
    
    // Calcular próximo múltiplo de 10°
    float nextAngleMark = floor(totalAngleRotated / 10.0f) * 10.0f + 10.0f;
    
    if (totalAngleRotated >= nextAngleMark && nextAngleMark != lastDebugAngle) {
      float progress = totalAngleRotated / 720.0f * 100.0f; // 720° = 2 vueltas
      logPrintf("DEBUG", "%.0f° | Pos: %.1f° | Pasos: %ld/6400 | Progreso: %.1f%%", 
               totalAngleRotated, currentAngle, (long)abs(rotateStepsCounter), progress);
      lastDebugAngle = nextAngleMark;
    }
    
    if (completed) {
      float completedRevs = (float)completedSteps / (float)stepsPerRev;
      float totalDegreesRotated = (float)completedSteps * degPerStep();
      float expectedDegrees = abs(rotateTargetSteps) * degPerStep();
      logPrintf("ROTAR", "Completado: %.2f vueltas (%.1f°) - %ld pasos", 
               completedRevs, totalDegreesRotated, (long)completedSteps);
      // Verificación final: ¿Realmente completamos el ángulo esperado?
      if (abs(totalDegreesRotated - expectedDegrees) > 1.0f) {
        logPrintf("WARNING", "Esperados %.1f°, completados %.1f° - Diferencia: %.1f°", 
                 expectedDegrees, totalDegreesRotated, totalDegreesRotated - expectedDegrees);
      }
      state = SysState::STOPPING;
      rotateMode = false;
      rotateStepsCounter = 0;
    }
  }
}

void controlStart() {
  esp_timer_create_args_t args;
  args.callback = &controlTick;
  args.arg = nullptr;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "ctrl";
  esp_timer_create(&args, &controlTimer);
  esp_timer_start_periodic(controlTimer, CONTROL_DT_US);

  // Crear timer para apagar el pulso (one-shot)
  esp_timer_create_args_t offArgs;
  offArgs.callback = &stepOffTick;
  offArgs.arg = nullptr;
  offArgs.dispatch_method = ESP_TIMER_TASK; // usar task-context para digitalWrite
  offArgs.name = "step_off";
  esp_timer_create(&offArgs, &stepOffTimer);

  // Crear timer para generar pasos (one-shot, reprogramado cada vez)
  esp_timer_create_args_t stepArgs;
  stepArgs.callback = &stepOnTick;
  stepArgs.arg = nullptr;
  stepArgs.dispatch_method = ESP_TIMER_TASK;
  stepArgs.name = "step";
  esp_timer_create(&stepArgs, &stepTimer);
  // Arranque perezoso: primera comprobación en 1 ms
  esp_timer_start_once(stepTimer, 1000);
}

} // namespace App

// ===== Implementación de los timers de STEP =====
namespace App {

static inline bool isMovingState() {
  return (state == SysState::RUNNING || state == SysState::ROTATING ||
          state == SysState::HOMING_SEEK);
}

static void IRAM_ATTR stepOffTick(void* /*arg*/) {
  digitalWrite(PIN_STEP, LOW);
  stepPinState = LOW;
}

static void IRAM_ATTR stepOnTick(void* /*arg*/) {
  // Leer velocidad actual y estado
  float v_now = v;

  // Límite de seguridad para no sobrecargar el planificador: 20 kpps
  const float MAX_PPS = 20000.0f;
  if (v_now < 0.0f) v_now = 0.0f;
  if (v_now > MAX_PPS) v_now = MAX_PPS;

  // ¿Debemos generar un paso?
  bool canStep = isMovingState() && (v_now > 1.0f);

  if (canStep) {
    // Subir STEP
    digitalWrite(PIN_STEP, HIGH);
    stepPinState = HIGH;

    // Apagar tras el ancho de pulso requerido
    if (stepOffTimer) {
      esp_timer_start_once(stepOffTimer, STEP_PULSE_WIDTH_US);
    }

    // Actualizar contadores de pasos y dirección
    uint32_t prevMod = modSteps();
    if (master_direction) {
      totalSteps++;
    } else {
      totalSteps--;
    }

    // Contadores específicos de modos
    if (state == SysState::HOMING_SEEK) {
      homingStepCounter++;
    }
    if (state == SysState::ROTATING) {
      if (rotateDirection) rotateStepsCounter++; else rotateStepsCounter--;
    }

    // ONE_REV: detectar cruce de cero y ordenar parada suave
    if (state == SysState::RUNNING && RUN_MODE == RunMode::ONE_REV) {
      uint32_t currMod = modSteps();
      if (crossedZeroThisTick(prevMod, currMod)) {
        forceStopTarget();
      }
    }
  }

  // Programar la siguiente llamada del generador de pasos
  uint64_t nextDelayUs;
  if (canStep) {
    // Calcular periodo según velocidad
    float v_sched = (v_now > 1.0f) ? v_now : 1.0f;
    float period_us = 1000000.0f / v_sched; // 1e6 / pps
    if (period_us < (float)(STEP_PULSE_WIDTH_US + 5)) {
      // Asegurar periodo mínimo mayor al ancho de pulso (margen 5us)
      period_us = (float)(STEP_PULSE_WIDTH_US + 5);
    }
    nextDelayUs = (uint64_t)period_us;
  } else {
    // Reintentar en 1 ms para ver si hay movimiento
    nextDelayUs = 1000;
  }

  if (stepTimer) {
    esp_timer_start_once(stepTimer, nextDelayUs);
  }
}

} // namespace App
