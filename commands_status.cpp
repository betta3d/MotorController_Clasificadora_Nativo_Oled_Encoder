#include "commands_status.h"
#include "globals.h"
#include "state.h"
#include "motion.h"
#include <Arduino.h>

using namespace App;

namespace Comandos {

void procesarGetGeneralStatus() {
    Serial.println("\n=== ESTADO SISTEMA MOTOR ===");
    Serial.printf("Estado:        %12s   Homed: %s\n", stateName(state), homed ? "SI" : "NO");
    Serial.printf("S-Curve:       %12s   Dir Master: %s\n", 
                  Cfg.enable_s_curve ? "ON" : "OFF", master_direction ? "CW" : "CCW");
    Serial.printf("Posicion:      %9.1f°   Sector: %s\n", 
                  currentAngleDeg(), sectorName(currentAngleDeg()));
    Serial.printf("Pasos Total:   %12lu\n", (unsigned long)totalSteps);
    
    if (state == SysState::ROTATING) {
      float progress = (float)abs(rotateStepsCounter) / (float)rotateTargetSteps * 100.0f;
      Serial.printf("ROTANDO: %ld/%ld pasos (%.1f%%)\n", 
                   (long)abs(rotateStepsCounter), (long)rotateTargetSteps, progress);
    }
    
    Serial.println("\n=== VELOCIDADES ===");
    Serial.printf("Lenta:       %6.1f cm/s  (V_SLOW=%.1f)\n", Cfg.v_slow_cmps, Cfg.v_slow_cmps);
    Serial.printf("Media:       %6.1f cm/s  (V_MED=%.1f)\n", Cfg.v_med_cmps, Cfg.v_med_cmps);
    Serial.printf("Rapida:      %6.1f cm/s  (V_FAST=%.1f)\n", Cfg.v_fast_cmps, Cfg.v_fast_cmps);
    Serial.printf("Homing:      %6.1f cm/s  (V_HOME=%.1f)\n", V_HOME_CMPS, V_HOME_CMPS);
    Serial.printf("Accel:       %6.1f cm/s² (ACCEL=%.1f)\n", Cfg.accel_cmps2, Cfg.accel_cmps2);
    Serial.printf("Jerk:        %6.1f cm/s³ (JERK=%.1f)\n", Cfg.jerk_cmps3, Cfg.jerk_cmps3);
    Serial.printf("Dist/Rev:    %6.2f cm    (CM_PER_REV=%.2f)\n", Cfg.cm_per_rev, Cfg.cm_per_rev);
    
    Serial.println("\n=== MECANICA ===");
    Serial.printf("Motor:         %6lu steps/rev  (MOTOR_STEPS=%lu)\n", MOTOR_FULL_STEPS_PER_REV, MOTOR_FULL_STEPS_PER_REV);
    Serial.printf("Microstepping: %4lu           (MICROSTEPPING=%lu)\n", MICROSTEPPING, MICROSTEPPING);
    Serial.printf("Engranaje:     %6.2f           (GEAR_RATIO=%.2f)\n", GEAR_RATIO, GEAR_RATIO);
    Serial.printf("Total/Rev:     %6lu steps     (Calculado)\n", stepsPerRev);
    
    Serial.println("\n=== SECTORES ANGULARES ===");
    Serial.printf("LENTO_UP:   %3.0f°-%3.0f°%-6s %s (Tomar huevo)\n", 
                  DEG_LENTO_UP.start, DEG_LENTO_UP.end, 
                  DEG_LENTO_UP.wraps ? "(wrap)" : "", "V_SLOW");
    Serial.printf("MEDIO:      %3.0f°-%3.0f°%-6s %s (Transporte)\n", 
                  DEG_MEDIO.start, DEG_MEDIO.end, 
                  DEG_MEDIO.wraps ? "(wrap)" : "", "V_MED");
    Serial.printf("LENTO_DOWN: %3.0f°-%3.0f°%-6s %s (Dejar huevo)\n", 
                  DEG_LENTO_DOWN.start, DEG_LENTO_DOWN.end, 
                  DEG_LENTO_DOWN.wraps ? "(wrap)" : "", "V_SLOW");
    Serial.printf("TRAVEL:     %3.0f°-%3.0f°%-6s %s (Retorno)\n", 
                  DEG_TRAVEL.start, DEG_TRAVEL.end, 
                  DEG_TRAVEL.wraps ? "(wrap)" : "", "V_FAST");
    
    Serial.println("\n=== HOMING ===");
    Serial.printf("Offset:         %6.1f°      (DEG_OFFSET=%.1f)\n", DEG_OFFSET, DEG_OFFSET);
    Serial.printf("Estabilizacion: %4lu ms     (T_ESTAB=%lu)\n", TIEMPO_ESTABILIZACION_HOME, TIEMPO_ESTABILIZACION_HOME);
  Serial.printf("Switch dir:     %6.2f vueltas (HOMING_SWITCH=%.2f)\n", HOMING_SWITCH_TURNS, HOMING_SWITCH_TURNS);
  Serial.printf("Timeout total:  %6.2f vueltas (HOMING_TIMEOUT=%.2f)\n", HOMING_TIMEOUT_TURNS, HOMING_TIMEOUT_TURNS);
  if (homingFaultCount > 0) {
    Serial.printf("Faults homing:  %6lu (desde arranque)\n", (unsigned long)homingFaultCount);
  }
    
    Serial.println("\n=== ESTADO ACTUAL ===");
    float ang = currentAngleDeg();
    const char* sector = sectorName(ang);
    Serial.printf("Posicion:   %7.1f°  (%s)\n", ang, sector);
    Serial.printf("Velocidad:  %7.1f pps (Objetivo: %.1f pps)\n", v, v_goal);
    Serial.printf("Aceleracion: %6.1f pps² (Max: %.1f pps²)\n", a, A_MAX);
    Serial.printf("Jerk Max:   %7.1f pps³\n", J_MAX);
    
    Serial.println("\n=== COMANDOS PRINCIPALES ===");
    Serial.println("Control:   HOME  ROTAR=N  STOP  SCURVE=ON/OFF  MASTER_DIR=CW/CCW");
    Serial.println("Config:    V_SLOW=5.0  V_MED=10.0  V_FAST=15.0");
    Serial.println("Homing:    V_HOME=8.0  DEG_OFFSET=45  T_ESTAB=2000");
    Serial.println("Info:      STATUS  HELP  LOG-STATUS");
    Serial.println();
}

} // namespace Comandos