#include "homing.h"
#include "globals.h"
#include "logger.h"

namespace App {

// Variables para proceso homing avanzado (ROTAR)
volatile int32_t rotarHomingStepsCounter = 0;
volatile int32_t rotarHomingOffsetCounter = 0;
volatile uint32_t rotarHomingStartTime = 0;
volatile bool rotarHomingFoundSensor = false;

void initRotarHoming() {
  rotarHomingStepsCounter = 0;
  rotarHomingOffsetCounter = 0;
  rotarHomingFoundSensor = false;
  rotarHomingStartTime = 0;
  
  // NOTA: Las variables v_goal, A_MAX, J_MAX se configuran en control.cpp ISR
  // No las configuramos aquí para evitar conflictos
  
  // Dirección CCW para buscar sensor
  setDirection(false); // CCW
  
  logPrint("HOME", "ROTAR: Iniciando búsqueda de sensor óptico (CCW)");
  logPrintf("HOME", "Máximo %.1f vueltas para encontrar sensor", 
           MAX_STEPS_TO_FIND_SENSOR / (float)stepsPerRev);
}

void processRotarHomingSeek() {
  // Verificar si encontró el sensor
  if (optActive() && !rotarHomingFoundSensor) {
    rotarHomingFoundSensor = true;
    logPrintf("HOME", "ROTAR: Sensor encontrado después de %ld pasos (%.2f vueltas)", 
             rotarHomingStepsCounter, rotarHomingStepsCounter / (float)stepsPerRev);
    return;
  }
  
  // El contador se incrementa automáticamente en el ISR de control.cpp
  // Solo verificar timeout - si da más vueltas de las configuradas sin encontrar sensor
  if (rotarHomingStepsCounter >= (int32_t)MAX_STEPS_TO_FIND_SENSOR) {
    logPrintf("ERROR", "ROTAR: Sensor no encontrado después de %.1f vueltas - FAULT",
             MAX_STEPS_TO_FIND_SENSOR / (float)stepsPerRev);
    state = SysState::FAULT;
  }
}

bool rotarHomingSeekCompleted() {
  return rotarHomingFoundSensor;
}

void processRotarHomingOffset() {
  // Calcular pasos necesarios para el offset
  int32_t offsetSteps = (int32_t)(DEG_OFFSET * stepsPerRev / 360.0f);
  
  // Si no hay offset, completar inmediatamente
  if (offsetSteps == 0) {
    return;
  }
  
  // Configurar movimiento CW para aplicar offset
  setDirection(true); // CW
  
  // El offset se aplicará automáticamente por el contador de pasos
  // Solo necesitamos verificar cuándo se complete
}

bool rotarHomingOffsetCompleted() {
  int32_t offsetSteps = (int32_t)(DEG_OFFSET * stepsPerRev / 360.0f);
  
  if (offsetSteps == 0) {
    logPrint("HOME", "ROTAR: Sin offset configurado - posición en sensor");
    return true;
  }
  
  // El contador se incrementa automáticamente en el ISR de control.cpp
  if (rotarHomingOffsetCounter >= offsetSteps) {
    logPrintf("HOME", "ROTAR: Offset aplicado - %.1f° desde sensor (%ld pasos)", 
             DEG_OFFSET, offsetSteps);
    return true;
  }
  
  return false;
}

void processRotarHomingStabilize() {
  // Detener movimiento durante estabilización
  v_goal = 0.0f;
  
  // Inicializar timer si es la primera vez
  if (rotarHomingStartTime == 0) {
    rotarHomingStartTime = millis();
    logPrintf("HOME", "ROTAR: Estabilizando por %lu ms en punto cero real", 
             TIEMPO_ESTABILIZACION_HOME);
  }
}

bool rotarHomingStabilizeCompleted() {
  if (rotarHomingStartTime == 0) {
    return false; // No ha iniciado aún
  }
  
  uint32_t elapsed = millis() - rotarHomingStartTime;
  
  if (elapsed >= TIEMPO_ESTABILIZACION_HOME) {
    logPrint("HOME", "ROTAR: Estabilización completada - punto cero establecido");
    return true;
  }
  
  return false;
}

} // namespace App