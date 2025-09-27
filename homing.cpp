#include "homing.h"
#include <cstdint>
#include <cstdlib>
#include "globals.h"
#include "logger.h"

// Variable específica de homing
float V_HOME_CMPS = 3.0f; // cm/s por defecto

namespace App {

HomingContext homingCtx = {HomingPhase::SEEK, 0, false, 0, false, 0, false};

void startCentralizedHoming() {
  // Reset completo del contexto de homing
  homingCtx.phase = HomingPhase::SEEK;
  homingCtx.baselineSteps = totalSteps;
  homingCtx.firstBaselineSteps = totalSteps;
  homingCtx.sensorFound = false;
  homingCtx.stabilizeStartMs = 0;
  homingCtx.triedAlternate = false;
  homingCtx.initialSelector = false; // siempre comenzamos inverse (selector=false)
  
  // Reset de variables que pueden interferir con homing
  homed = false;
  homingStepCounter = 0;
  v = 0.0f;        // Reset velocidad actual
  a = 0.0f;        // Reset aceleración actual
  v_goal = 0.0f;   // Reset velocidad objetivo inicial
  
  // Reset de modo rotación que puede interferir
  rotateMode = false;
  rotateStepsCounter = 0;
  // NO limpiar pendingRotateRevs aquí: puede contener una rotación solicitada
  // que debe ejecutarse al finalizar el homing. Se consumirá más tarde si procede.
  
  // Configurar dirección para buscar sensor: primer intento inverse (selector=false)
  setDirection(false);
  
  // Calcular velocidad de homing en pasos/s desde V_HOME_CMPS
  float steps_per_cm = (Cfg.cm_per_rev > 0.0f) ? ((float)stepsPerRev / Cfg.cm_per_rev) : 0.0f;
  v_goal = V_HOME_CMPS * steps_per_cm; // Usar V_HOME_CMPS
  
  // Establecer estado del sistema
  state = SysState::HOMING_SEEK; // Estado global para movimiento
  
  logPrintf("HOME", "Inicio homing centralizado - master=%d inverse=%d selector(inverse)=%d totalSteps=%lld", 
            master_direction?1:0, inverse_direction?1:0, currentDirIsMaster?1:0, (long long)totalSteps);
}

void processCentralizedHoming() {
  switch (homingCtx.phase) {
    case HomingPhase::SEEK: {
      // Debouncing simple: requiere 3 lecturas consecutivas
      static int sensorCount = 3;
      // Calcular velocidad de homing en pasos/s desde V_HOME_CMPS
      float steps_per_cm = (Cfg.cm_per_rev > 0.0f) ? ((float)stepsPerRev / Cfg.cm_per_rev) : 0.0f;
      v_goal = V_HOME_CMPS * steps_per_cm; // Usar V_HOME_CMPS
      state = SysState::HOMING_SEEK;
      if (optActive()) {
        sensorCount++;
        if (sensorCount >= 3) {
          homingCtx.sensorFound = true;
          homingCtx.phase = HomingPhase::STABILIZE; // Primero estabilizar tras detectar sensor
          homingCtx.stabilizeStartMs = millis();
          v_goal = 0.0f; // Detener motor
          state = SysState::HOMING_SEEK; // Mantener en HOMING_SEEK durante estabilización
          logPrint("HOME", "Sensor detectado, estabilizando");
        }
      } else {
        sensorCount = 0;
      }
      // Timeout por vueltas
      float vueltasLocal = fabsf((float)(totalSteps - homingCtx.baselineSteps)) / (float)stepsPerRev;
      float vueltasTotal = fabsf((float)(totalSteps - homingCtx.firstBaselineSteps)) / (float)stepsPerRev;
      // Telemetría periódica (cada ~0.25 s)
      static uint32_t lastDbgMs = 0; 
      if (millis() - lastDbgMs > 250) {
        lastDbgMs = millis();
        logPrintf("HOME_DBG", "selMaster=%d phys=%d local=%.3f total=%.3f opt=%d triedAlt=%d", 
                  currentDirIsMaster?1:0,
                  (currentDirIsMaster?master_direction:inverse_direction)?1:0,
                  vueltasLocal, vueltasTotal, optActive()?1:0, homingCtx.triedAlternate?1:0);
      }
      // Si no detectamos sensor en ~0.7 vuelta local: cambiar dirección (una sola vez)
      if (!homingCtx.sensorFound && !homingCtx.triedAlternate && vueltasLocal > 0.7f) {
        homingCtx.triedAlternate = true;
        homingCtx.baselineSteps = totalSteps; // reset local
        setDirection(true); // ahora usar dirección maestra
        logPrint("HOME", "No sensor en primera dirección, invirtiendo para segundo intento");
      }
      // Timeout total tras ~1.4 vueltas sumadas (ambas direcciones)
      if (!homingCtx.sensorFound && vueltasTotal > 1.4f) {
        homingCtx.phase = HomingPhase::FAULT;
        state = SysState::FAULT;
        v_goal = 0.0f;
        logPrint("HOME", "Timeout homing: no se detectó sensor tras ambos sentidos");
      }
      break;
    }
    case HomingPhase::OFFSET: {
      int32_t offsetSteps = (int32_t)(abs(DEG_OFFSET) * stepsPerRev / 360.0f);
      int64_t delta = llabs(totalSteps - homingCtx.baselineSteps);
      // Calcular velocidad de homing en pasos/s desde V_HOME_CMPS
      float steps_per_cm = (Cfg.cm_per_rev > 0.0f) ? ((float)stepsPerRev / Cfg.cm_per_rev) : 0.0f;
      v_goal = V_HOME_CMPS * steps_per_cm; // Usar V_HOME_CMPS
      state = SysState::HOMING_SEEK; // Mantener en HOMING_SEEK durante offset
      if (delta >= offsetSteps) {
        // Offset completado, hacer estabilización final
        homingCtx.phase = HomingPhase::DONE;
        homingCtx.stabilizeStartMs = millis();
        v_goal = 0.0f;
        state = SysState::HOMING_SEEK; // Mantener durante estabilización final
        logPrint("HOME", "Offset aplicado, estabilización final en punto cero real");
      }
      break;
    }
    case HomingPhase::STABILIZE: {
      v_goal = 0.0f;
      state = SysState::HOMING_SEEK; // Mantener en HOMING_SEEK durante estabilización
      if (millis() - homingCtx.stabilizeStartMs >= TIEMPO_ESTABILIZACION_HOME) {
        if (homingCtx.sensorFound) {
          // Tras estabilizar en sensor, ir a OFFSET
          homingCtx.phase = HomingPhase::OFFSET;
          homingCtx.baselineSteps = totalSteps;
          // Offset: selector depende del signo de DEG_OFFSET (>=0 usar master, <0 usar inverse)
          setDirection(DEG_OFFSET >= 0);
          logPrint("HOME", "Estabilización completada, aplicando offset");
        }
      }
      break;
    }
    case HomingPhase::DONE: {
      v_goal = 0.0f;
      state = SysState::HOMING_SEEK; // Mantener durante estabilización final
      if (millis() - homingCtx.stabilizeStartMs >= TIEMPO_ESTABILIZACION_HOME) {
        // Estabilización final completada - establecer punto cero y finalizar
        setZeroHere(); // Esto establece homed = true y totalSteps = 0
        
        // Limpiar variables del sistema de homing
        v_goal = 0.0f;
        v = 0.0f;
        a = 0.0f;
        homingStepCounter = 0;
        
        // Estado final
        state = SysState::READY;
        logPrint("HOME", "Homing completado, cero fijado en punto real - READY");
      }
      break;
    }
    case HomingPhase::FAULT:
      // Limpiar completamente en caso de error
      v_goal = 0.0f;
      v = 0.0f;
      a = 0.0f;
      rotateMode = false;
      state = SysState::FAULT;
      logPrint("HOME", "Homing FAULT - sistema detenido");
      break;
  }
}

bool centralizedHomingCompleted() {
  return (homingCtx.phase == HomingPhase::DONE || homingCtx.phase == HomingPhase::FAULT);
}

} // namespace App