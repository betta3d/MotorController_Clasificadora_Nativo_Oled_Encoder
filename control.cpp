#include "control.h"
#include "globals.h"
#include "motion.h"
#include "pins.h"
#include "logger.h"
#include <Arduino.h>
#include <esp_timer.h>

namespace App {

static void IRAM_ATTR controlTick(void* /*arg*/) {
  const float dt    = (float)CONTROL_DT_US * 1e-6f;
  const float dt_us = (float)CONTROL_DT_US;

  switch (state) {
    case SysState::RUNNING: {
      // Modo normal por sectores
      float deg = currentAngleDeg();
      selectSectorProfile(deg);
      setDirection(true); // CW
    } break;

    case SysState::ROTATING: {
      // Modo rotación N vueltas - con velocidades por ángulo como RUNNING
      float deg = currentAngleDeg();
      selectSectorProfile(deg);
      setDirection(rotateDirection); // Mantener dirección de la rotación
      
      // DEBUG: Mostrar estado cada 100 ticks (0.1 segundos)
      static uint32_t debugCounter = 0;
      debugCounter++;
      if (debugCounter % 100 == 0) { // Cada 100ms
        logPrintf("DEBUG", "ROTATING: deg=%.1f, v_goal=%.1f, A_MAX=%.1f, sector=%s", 
                 currentAngleDeg(), v_goal, A_MAX, sectorName(currentAngleDeg()));
      }
    } break;

    case SysState::HOMING_SEEK:
      v_goal = HOMING_V_SEEK_PPS;  A_MAX = HOMING_A_SEEK_PPS2;  J_MAX = HOMING_J_SEEK_PPS3;
      setDirection(HOMING_SEEK_DIR_CW);
      break;

    case SysState::HOMING_BACKOFF:
      v_goal = HOMING_V_SEEK_PPS;  A_MAX = HOMING_A_SEEK_PPS2;  J_MAX = HOMING_J_SEEK_PPS3;
      setDirection(!HOMING_SEEK_DIR_CW);
      break;

    case SysState::HOMING_REAPP:
      v_goal = HOMING_V_REAPP_PPS; A_MAX = HOMING_A_REAPP_PPS2; J_MAX = HOMING_J_REAPP_PPS3;
      setDirection(HOMING_SEEK_DIR_CW);
      break;

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
  if (Cfg.enable_s_curve && (state != SysState::HOMING_SEEK && state != SysState::HOMING_BACKOFF && state != SysState::HOMING_REAPP)) {
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

  // Pulsos STEP
  uint32_t currModBefore = modSteps();

  if (v > 1.0f && (state == SysState::RUNNING || state == SysState::ROTATING || state == SysState::HOMING_SEEK || state == SysState::HOMING_BACKOFF || state == SysState::HOMING_REAPP)) {
    // CRÍTICO: ISR ejecuta cada 1ms (1000μs)
    // v_max_real = 1000pps para respetar 1kHz ISR
    float v_real = (v > 1000.0f) ? 1000.0f : v;
    const float period_us = 1000.0f; // Fijo: 1 pulso cada 1ms máximo
    stepAccumulatorUs += dt_us;

    if (stepPinState == HIGH) {
      pulseHoldUs += (uint32_t)dt_us;
      if (pulseHoldUs >= STEP_PULSE_WIDTH_US) {
        digitalWrite(PIN_STEP, LOW);
        stepPinState = LOW;
        pulseHoldUs = 0;
      }
    }

    while (stepAccumulatorUs >= period_us) {
      digitalWrite(PIN_STEP, HIGH);
      stepPinState = HIGH;
      pulseHoldUs  = 0;

      // Actualizar totalSteps según dirección
      if (state == SysState::ROTATING && !rotateDirection) {
        totalSteps--; // CCW en modo ROTATING
      } else {
        totalSteps++; // CW por defecto
      }
      stepAccumulatorUs -= period_us;

      if (state == SysState::HOMING_SEEK || state == SysState::HOMING_BACKOFF || state == SysState::HOMING_REAPP) {
        homingStepCounter++;
      }
      
      // Contador independiente para ROTATING
      if (state == SysState::ROTATING) {
        if (rotateDirection) {
          rotateStepsCounter++;
        } else {
          rotateStepsCounter--;
        }
      }
    }
  } else {
    if (stepPinState == HIGH) {
      digitalWrite(PIN_STEP, LOW);
      stepPinState = LOW;
      pulseHoldUs = 0;
    }
    stepAccumulatorUs = 0.0f;
  }

  // ONE_REV: una vuelta y parar suave
  if (state == SysState::RUNNING && RUN_MODE == RunMode::ONE_REV) {
    uint32_t currModAfter = modSteps();
    if (crossedZeroThisTick(currModBefore, currModAfter)) {
      forceStopTarget();
    }
  }

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
      logPrintf("ROTAR", "Completado: %.2f vueltas (%.1f°) - %ld pasos", 
               completedRevs, totalDegreesRotated, (long)completedSteps);
      
      // Verificación final: ¿Realmente completamos 720°?
      if (abs(totalDegreesRotated - 720.0f) > 1.0f) {
        logPrintf("WARNING", "Esperados 720°, completados %.1f° - Diferencia: %.1f°", 
                 totalDegreesRotated, totalDegreesRotated - 720.0f);
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
}

} // namespace App
