#include "commands.h"
#include "globals.h"
#include "state.h"
#include "control.h"
#include "motion.h"
#include "eeprom_store.h"
#include "oled_ui.h"
#include "logger.h"

using namespace App;

// Función auxiliar para normalizar comandos a mayúsculas
String normalizeCommand(const String& cmd) {
  String normalized = cmd;
  normalized.toUpperCase();
  return normalized;
}

// Función auxiliar para parsear rangos de sectores (ya existía en MotorController.ino)
bool parseSectorRange(const String& rangeStr, float& start, float& end, bool& wraps) {
  int dashIndex = rangeStr.indexOf('-');
  if (dashIndex == -1) return false;
  
  start = rangeStr.substring(0, dashIndex).toFloat();
  end = rangeStr.substring(dashIndex + 1).toFloat();
  
  // Normalizar ángulos a rango 0-360
  while (start < 0) start += 360;
  while (start >= 360) start -= 360;
  while (end < 0) end += 360;
  while (end >= 360) end -= 360;
  
  // Determinar si el sector cruza 0° (wrap around)
  wraps = (start > end);
  
  return true;
}

void processCommands() {
  if (!Serial.available()) return;
  
  String line = Serial.readStringUntil('\n'); 
  line.trim();
  
  // Normalizar comando para comparación (mayúsculas)
  String upperLine = normalizeCommand(line);
  
  float value = 0;
  
  // ===== COMANDOS DE CONTROL =====
  if (upperLine.equals("SCURVE=ON")) {
    Cfg.enable_s_curve = true;
    saveConfig(); // Guardar en EEPROM
    logPrint("CONFIG", "S-CURVE=ON (rampas suaves habilitadas)");
    
  } else if (upperLine.equals("SCURVE=OFF")) {
    Cfg.enable_s_curve = false;
    saveConfig(); // Guardar en EEPROM
    logPrint("CONFIG", "S-CURVE=OFF (control directo de velocidad)");
    
  } else if (upperLine.startsWith("ROTAR=")) {
    value = upperLine.substring(6).toFloat();
    if (value != 0) {
      // Si está en RUNNING, primero detener
      if (state == SysState::RUNNING) {
        state = SysState::STOPPING;
        logPrint("ROTAR", "Deteniendo RUNNING primero...");
        // Esperar un momento para que se detenga
        delay(100);
      }
      
      // Configurar rotación
      applyConfigToProfiles(); // Asegurar perfiles actualizados
      rotateTargetRevs = value;
      rotateTargetSteps = (int32_t)(abs(value) * (float)stepsPerRev);
      rotateDirection = (value > 0); // positivo=CW, negativo=CCW
      rotateStepsCounter = 0; // Resetear contador
      rotateMode = true;
      
      // DEBUG: Mostrar valores de cálculo
      logPrint("DEBUG", "ROTAR iniciado - revisa físicamente y usa STOP cuando complete las vueltas reales");
      logPrintf("DEBUG", "stepsPerRev=%lu, value=%.1f, rotateTargetSteps=%ld", 
               stepsPerRev, value, (long)rotateTargetSteps);
      logPrintf("DEBUG", "Observa cuándo complete %.1f vueltas físicas y usa STOP", value);
      
      // ROTAR funciona sin HOME: setear temporalmente homed=true
      // Considerar posición actual como referencia (ángulo 0)
      if (!homed) {
        homed = true;
        totalSteps = 0; // Resetear posición actual como origen
        logPrint("ROTAR", "Posicion actual establecida como referencia (0°)");
      }
      
      // Iniciar rotación
      uint32_t now = millis();
      v_goal = 0.0f; a = 0.0f;
      state = SysState::ROTATING;
      uiScreen = UiScreen::STATUS;
      screensaverActive = false;
      screensaverStartTime = now;
      
      Serial.printf("[ROTAR] Iniciando %.1f vueltas (%s) - %ld pasos objetivo\n", 
                   value, (value > 0) ? "CW" : "CCW", (long)rotateTargetSteps);
    } else {
      logPrint("ERROR", "ROTAR requiere valor diferente de 0");
    }
    
  } else if (upperLine.equals("STOP")) {
    if (state == SysState::ROTATING) {
      // Mostrar pasos reales ejecutados antes de resetear
      float realRevs = (float)abs(rotateStepsCounter) / (float)stepsPerRev;
      float realDegrees = (float)abs(rotateStepsCounter) * degPerStep();
      Serial.printf("[STOP] ROTACION DETENIDA - Pasos ejecutados: %ld (%.2f vueltas, %.1f°)\n", 
                   (long)abs(rotateStepsCounter), realRevs, realDegrees);
      
      // Calcular factor de corrección
      if (rotateTargetRevs != 0) {
        float factor = realRevs / rotateTargetRevs;
        Serial.printf("[CALIBRACION] Factor real: %.3f (usar MICROSTEPPING=%.0f si es diferente a 16)\n", 
                     factor, 16.0f * factor);
      }
      
      state = SysState::STOPPING;
      rotateMode = false;
      rotateStepsCounter = 0;
    } else if (state == SysState::RUNNING) {
      state = SysState::STOPPING;
      Serial.println("[STOP] MOTOR DETENIENDO");
    } else {
      Serial.println("[STOP] Motor ya detenido");
    }
    
  } else if (upperLine.equals("STATUS")) {
    Serial.println("=== ESTADO SISTEMA ===");
    Serial.printf("STATE: %s | HOMED: %s | S-CURVE: %s\n",
                 stateName(state), homed ? "YES" : "NO", 
                 Cfg.enable_s_curve ? "ON" : "OFF");
    
    if (state == SysState::ROTATING) {
      float progress = (float)rotateStepsCounter / (float)rotateTargetSteps * 100.0f;
      Serial.printf("ROTAR: %ld/%ld pasos (%.1f%%) - %.1f vueltas objetivo\n", 
                   (long)rotateStepsCounter, (long)rotateTargetSteps, progress, rotateTargetRevs);
    }
    
    Serial.println("\n=== CONFIGURACION PERSISTENTE (EEPROM) ===");
    Serial.printf("cm_per_rev: %.2f cm/rev - Distancia por vuelta completa (CM_PER_REV=25.4)\n", Cfg.cm_per_rev);
    Serial.printf("v_slow_cmps: %.1f cm/s - Velocidad lenta (V_SLOW=5.0)\n", Cfg.v_slow_cmps);
    Serial.printf("v_med_cmps: %.1f cm/s - Velocidad media (V_MED=10.0)\n", Cfg.v_med_cmps);
    Serial.printf("v_fast_cmps: %.1f cm/s - Velocidad rapida (V_FAST=15.0)\n", Cfg.v_fast_cmps);
    Serial.printf("accel_cmps2: %.1f cm/s² - Aceleracion maxima (ACCEL=50.0)\n", Cfg.accel_cmps2);
    Serial.printf("jerk_cmps3: %.1f cm/s³ - Jerk para curvas S (JERK=100.0)\n", Cfg.jerk_cmps3);
    Serial.printf("enable_s_curve: %s - Control suave aceleracion (SCURVE=ON/OFF)\n", Cfg.enable_s_curve ? "ON" : "OFF");
    
    Serial.println("\n=== PARAMETROS MECANICOS ===");
    Serial.printf("MOTOR_STEPS_REV: %lu steps/rev - Pasos motor por vuelta (MOTOR_STEPS=200)\n", MOTOR_FULL_STEPS_PER_REV);
    Serial.printf("MICROSTEPPING: %lu - Factor micropasos driver (MICROSTEPPING=16)\n", MICROSTEPPING);
    Serial.printf("GEAR_RATIO: %.2f - Relacion engranajes (GEAR_RATIO=1.0)\n", GEAR_RATIO);
    Serial.printf("stepsPerRev: %lu - Pasos totales por revolucion (calculado)\n", stepsPerRev);
    
    Serial.println("\n=== SECTORES ANGULARES ===");
    Serial.printf("DEG_LENTO: %.0f°-%.0f°%s - Zona lenta tomar/soltar huevo (DEG_LENTO=355-10)\n", 
                 DEG_LENTO.start, DEG_LENTO.end, DEG_LENTO.wraps ? " (wrap)" : "");
    Serial.printf("DEG_MEDIO: %.0f°-%.0f°%s - Zona media transporte (DEG_MEDIO=10-180)\n", 
                 DEG_MEDIO.start, DEG_MEDIO.end, DEG_MEDIO.wraps ? " (wrap)" : "");
    Serial.printf("DEG_RAPIDO: %.0f°-%.0f°%s - Zona rapida retorno vacio (DEG_RAPIDO=180-355)\n", 
                 DEG_RAPIDO.start, DEG_RAPIDO.end, DEG_RAPIDO.wraps ? " (wrap)" : "");
    
    Serial.println("\n=== PARAMETROS HOMING ===");
    Serial.printf("SEEK_DIR: %s - Direccion busqueda inicial (HOMING_SEEK_DIR=CW/CCW)\n", HOMING_SEEK_DIR_CW ? "CW" : "CCW");
    Serial.printf("SEEK_VEL: %.0f pps - Velocidad busqueda sensor (HOMING_SEEK_VEL=800)\n", HOMING_V_SEEK_PPS);
    Serial.printf("SEEK_ACCEL: %.0f pps² - Aceleracion busqueda (HOMING_SEEK_ACCEL=3000)\n", HOMING_A_SEEK_PPS2);
    Serial.printf("SEEK_JERK: %.0f pps³ - Jerk busqueda (HOMING_SEEK_JERK=20000)\n", HOMING_J_SEEK_PPS3);
    Serial.printf("REAPP_VEL: %.0f pps - Velocidad reaproximacion (HOMING_REAPP_VEL=400)\n", HOMING_V_REAPP_PPS);
    Serial.printf("REAPP_ACCEL: %.0f pps² - Aceleracion reaproximacion (HOMING_REAPP_ACCEL=2000)\n", HOMING_A_REAPP_PPS2);
    Serial.printf("REAPP_JERK: %.0f pps³ - Jerk reaproximacion (HOMING_REAPP_JERK=15000)\n", HOMING_J_REAPP_PPS3);
    Serial.printf("BACKOFF_DEG: %.1f° - Retroceso desde sensor (HOMING_BACKOFF=3.0)\n", HOMING_BACKOFF_DEG);
    Serial.printf("TIMEOUT_STEPS: %lu - Limite pasos busqueda (calculado)\n", HOMING_TIMEOUT_STEPS);
    
    Serial.println("\n=== ESTADO CINEMATICO ACTUAL ===");
    float ang = currentAngleDeg();
    Serial.printf("Posicion: %.1f° (%s) | %lu pasos totales\n", ang, sectorName(ang), (unsigned long)totalSteps);
    Serial.printf("v: %.1f pps | a: %.1f pps² | v_goal: %.1f pps\n", v, a, v_goal);
    Serial.printf("A_MAX: %.1f pps² | J_MAX: %.1f pps³\n", A_MAX, J_MAX);
    
    Serial.println("\n=== COMANDOS DISPONIBLES ===");
    Serial.println("CONFIG: CM_PER_REV=25.4 | V_SLOW=5.0 | V_MED=10.0 | V_FAST=15.0 | ACCEL=50.0 | JERK=100.0");
    Serial.println("MECANICO: MOTOR_STEPS=200 | MICROSTEPPING=16 | GEAR_RATIO=1.0");
    Serial.println("SECTORES: DEG_LENTO=355-10 | DEG_MEDIO=10-180 | DEG_RAPIDO=180-355"); 
    Serial.println("HOMING: HOMING_SEEK_DIR=CW | HOMING_SEEK_VEL=800 | HOMING_BACKOFF=3.0 | etc");
    Serial.println("CONTROL: SCURVE=ON/OFF | ROTAR=N | STOP | STATUS");
    
  // ===== COMANDOS SECTORES ANGULARES =====
  } else if (upperLine.startsWith("DEG_LENTO=")) {
    String rangeStr = upperLine.substring(10);
    float start, end;
    bool wraps;
    if (parseSectorRange(rangeStr, start, end, wraps)) {
      DEG_LENTO.start = start;
      DEG_LENTO.end = end;
      DEG_LENTO.wraps = wraps;
      Serial.printf("DEG_LENTO actualizado: %.0f-%.0f%s\n", start, end, wraps ? " (wrap)" : "");
    } else {
      Serial.println("ERROR: Formato incorrecto. Use DEG_LENTO=start-end (ej: 355-10)");
    }
    
  } else if (upperLine.startsWith("DEG_MEDIO=")) {
    String rangeStr = upperLine.substring(10);
    float start, end;
    bool wraps;
    if (parseSectorRange(rangeStr, start, end, wraps)) {
      DEG_MEDIO.start = start;
      DEG_MEDIO.end = end;
      DEG_MEDIO.wraps = wraps;
      Serial.printf("DEG_MEDIO actualizado: %.0f-%.0f%s\n", start, end, wraps ? " (wrap)" : "");
    } else {
      Serial.println("ERROR: Formato incorrecto. Use DEG_MEDIO=start-end (ej: 10-180)");
    }
    
  } else if (upperLine.startsWith("DEG_RAPIDO=")) {
    String rangeStr = upperLine.substring(11);
    float start, end;
    bool wraps;
    if (parseSectorRange(rangeStr, start, end, wraps)) {
      DEG_RAPIDO.start = start;
      DEG_RAPIDO.end = end;
      DEG_RAPIDO.wraps = wraps;
      Serial.printf("DEG_RAPIDO actualizado: %.0f-%.0f%s\n", start, end, wraps ? " (wrap)" : "");
    } else {
      Serial.println("ERROR: Formato incorrecto. Use DEG_RAPIDO=start-end (ej: 180-355)");
    }
    
  // ===== COMANDOS CONFIGURACION EEPROM =====
  } else if (upperLine.startsWith("CM_PER_REV=")) {
    float value = upperLine.substring(11).toFloat();
    if (value > 0) {
      Cfg.cm_per_rev = value;
      saveConfig(); // Guardar en EEPROM
      applyConfigToProfiles(); // Aplicar cambios a perfiles de movimiento
      Serial.printf("CM_PER_REV actualizado: %.2f cm/rev\n", value);
    } else {
      logPrint("ERROR", "CM_PER_REV debe ser > 0");
    }
    
  } else if (upperLine.startsWith("V_SLOW=")) {
    float value = upperLine.substring(7).toFloat();
    if (value > 0) {
      Cfg.v_slow_cmps = value;
      saveConfig(); // Guardar en EEPROM
      applyConfigToProfiles(); // Aplicar cambios a perfiles de movimiento
      Serial.printf("V_SLOW actualizado: %.1f cm/s\n", value);
    } else {
      Serial.println("ERROR: V_SLOW debe ser > 0");
    }
    
  } else if (upperLine.startsWith("V_MED=")) {
    float value = upperLine.substring(6).toFloat();
    if (value > 0) {
      Cfg.v_med_cmps = value;
      saveConfig(); // Guardar en EEPROM
      applyConfigToProfiles(); // Aplicar cambios a perfiles de movimiento
      Serial.printf("V_MED actualizado: %.1f cm/s\n", value);
    } else {
      Serial.println("ERROR: V_MED debe ser > 0");
    }
    
  } else if (upperLine.startsWith("V_FAST=")) {
    float value = upperLine.substring(7).toFloat();
    if (value > 0) {
      Cfg.v_fast_cmps = value;
      saveConfig(); // Guardar en EEPROM
      applyConfigToProfiles(); // Aplicar cambios a perfiles de movimiento
      Serial.printf("V_FAST actualizado: %.1f cm/s\n", value);
    } else {
      Serial.println("ERROR: V_FAST debe ser > 0");
    }
    
  } else if (upperLine.startsWith("ACCEL=")) {
    float value = upperLine.substring(6).toFloat();
    if (value > 0) {
      Cfg.accel_cmps2 = value;
      saveConfig(); // Guardar en EEPROM
      applyConfigToProfiles(); // Aplicar cambios a perfiles de movimiento
      Serial.printf("ACCEL actualizado: %.1f cm/s²\n", value);
    } else {
      Serial.println("ERROR: ACCEL debe ser > 0");
    }
    
  } else if (upperLine.startsWith("JERK=")) {
    float value = upperLine.substring(5).toFloat();
    if (value > 0) {
      Cfg.jerk_cmps3 = value;
      saveConfig(); // Guardar en EEPROM
      applyConfigToProfiles(); // Aplicar cambios a perfiles de movimiento
      Serial.printf("JERK actualizado: %.1f cm/s³\n", value);
    } else {
      Serial.println("ERROR: JERK debe ser > 0");
    }
    
  // ===== COMANDOS PARAMETROS MECANICOS =====
  } else if (upperLine.startsWith("MOTOR_STEPS=")) {
    uint32_t value = upperLine.substring(12).toInt();
    if (value > 0) {
      MOTOR_FULL_STEPS_PER_REV = value;
      stepsPerRev = (uint32_t)(MOTOR_FULL_STEPS_PER_REV * MICROSTEPPING * GEAR_RATIO);
      HOMING_TIMEOUT_STEPS = 5 * stepsPerRev;
      Serial.printf("MOTOR_STEPS actualizado: %lu steps/rev\n", value);
    } else {
      logPrint("ERROR", "MOTOR_STEPS debe ser > 0");
    }
    
  } else if (upperLine.startsWith("MICROSTEPPING=")) {
    uint32_t value = upperLine.substring(14).toInt();
    if (value > 0) {
      MICROSTEPPING = value;
      stepsPerRev = (uint32_t)(MOTOR_FULL_STEPS_PER_REV * MICROSTEPPING * GEAR_RATIO);
      HOMING_TIMEOUT_STEPS = 5 * stepsPerRev;
      Serial.printf("MICROSTEPPING actualizado: %lu\n", value);
    } else {
      Serial.println("ERROR: MICROSTEPPING debe ser > 0");
    }
    
  } else if (upperLine.startsWith("GEAR_RATIO=")) {
    float value = upperLine.substring(11).toFloat();
    if (value > 0) {
      GEAR_RATIO = value;
      stepsPerRev = (uint32_t)(MOTOR_FULL_STEPS_PER_REV * MICROSTEPPING * GEAR_RATIO);
      HOMING_TIMEOUT_STEPS = 5 * stepsPerRev;
      Serial.printf("GEAR_RATIO actualizado: %.2f\n", value);
    } else {
      Serial.println("ERROR: GEAR_RATIO debe ser > 0");
    }
    
  // ===== COMANDOS PARAMETROS HOMING =====
  } else if (upperLine.startsWith("HOMING_SEEK_DIR=")) {
    String value = upperLine.substring(16);
    if (value.equals("CW")) {
      HOMING_SEEK_DIR_CW = true;
      Serial.println("HOMING_SEEK_DIR actualizado: CW");
    } else if (value.equals("CCW")) {
      HOMING_SEEK_DIR_CW = false;
      Serial.println("HOMING_SEEK_DIR actualizado: CCW");
    } else {
      Serial.println("ERROR: Use HOMING_SEEK_DIR=CW o HOMING_SEEK_DIR=CCW");
    }
    
  } else if (upperLine.startsWith("HOMING_SEEK_VEL=")) {
    float value = upperLine.substring(16).toFloat();
    if (value > 0) {
      HOMING_V_SEEK_PPS = value;
      Serial.printf("HOMING_SEEK_VEL actualizado: %.0f pps\n", value);
    } else {
      Serial.println("ERROR: HOMING_SEEK_VEL debe ser > 0");
    }
    
  } else if (upperLine.startsWith("HOMING_SEEK_ACCEL=")) {
    float value = upperLine.substring(18).toFloat();
    if (value > 0) {
      HOMING_A_SEEK_PPS2 = value;
      Serial.printf("HOMING_SEEK_ACCEL actualizado: %.0f pps²\n", value);
    } else {
      Serial.println("ERROR: HOMING_SEEK_ACCEL debe ser > 0");
    }
    
  } else if (upperLine.startsWith("HOMING_SEEK_JERK=")) {
    float value = upperLine.substring(17).toFloat();
    if (value > 0) {
      HOMING_J_SEEK_PPS3 = value;
      Serial.printf("HOMING_SEEK_JERK actualizado: %.0f pps³\n", value);
    } else {
      Serial.println("ERROR: HOMING_SEEK_JERK debe ser > 0");
    }
    
  } else if (upperLine.startsWith("HOMING_REAPP_VEL=")) {
    float value = upperLine.substring(17).toFloat();
    if (value > 0) {
      HOMING_V_REAPP_PPS = value;
      Serial.printf("HOMING_REAPP_VEL actualizado: %.0f pps\n", value);
    } else {
      Serial.println("ERROR: HOMING_REAPP_VEL debe ser > 0");
    }
    
  } else if (upperLine.startsWith("HOMING_REAPP_ACCEL=")) {
    float value = upperLine.substring(19).toFloat();
    if (value > 0) {
      HOMING_A_REAPP_PPS2 = value;
      Serial.printf("HOMING_REAPP_ACCEL actualizado: %.0f pps²\n", value);
    } else {
      Serial.println("ERROR: HOMING_REAPP_ACCEL debe ser > 0");
    }
    
  } else if (upperLine.startsWith("HOMING_REAPP_JERK=")) {
    float value = upperLine.substring(18).toFloat();
    if (value > 0) {
      HOMING_J_REAPP_PPS3 = value;
      Serial.printf("HOMING_REAPP_JERK actualizado: %.0f pps³\n", value);
    } else {
      Serial.println("ERROR: HOMING_REAPP_JERK debe ser > 0");
    }
    
  } else if (upperLine.startsWith("HOMING_BACKOFF=")) {
    float value = upperLine.substring(15).toFloat();
    if (value >= 0) {
      HOMING_BACKOFF_DEG = value;
      logPrintf("CONFIG", "HOMING_BACKOFF actualizado: %.1f°", value);
    } else {
      logPrint("ERROR", "HOMING_BACKOFF debe ser >= 0");
    }
    
  // ===== COMANDOS LOG CONTROL =====
  } else if (upperLine.startsWith("LOG-") && upperLine.indexOf("=") > 0) {
    // Formato: LOG-CATEGORIA=ON/OFF
    int dashPos = upperLine.indexOf('-');
    int equalPos = upperLine.indexOf('=');
    
    if (dashPos > 0 && equalPos > dashPos) {
      String category = upperLine.substring(dashPos + 1, equalPos);
      String value = upperLine.substring(equalPos + 1);
      
      if (value == "ON") {
        if (setLogEnabled(category, true)) {
          logPrintf("CONFIG", "LOG-%s habilitado", category.c_str());
        } else {
          logPrintf("ERROR", "Categoría LOG-%s desconocida", category.c_str());
        }
      } else if (value == "OFF") {
        if (setLogEnabled(category, false)) {
          logPrintf("CONFIG", "LOG-%s deshabilitado", category.c_str());
        } else {
          logPrintf("ERROR", "Categoría LOG-%s desconocida", category.c_str());
        }
      } else {
        logPrint("ERROR", "Use LOG-CATEGORIA=ON o LOG-CATEGORIA=OFF");
      }
    } else {
      logPrint("ERROR", "Formato incorrecto. Use LOG-CATEGORIA=ON/OFF");
    }
    
  } else if (upperLine.equals("LOG-STATUS")) {
    showLogStatus();
  }
  
  // Si llegamos aquí y no se procesó ningún comando, mostrar error
  else if (line.length() > 0) {
    logPrintf("ERROR", "Comando desconocido: %s", line.c_str());
    logPrint("ERROR", "Use STATUS para ver todos los comandos disponibles");
  }
}