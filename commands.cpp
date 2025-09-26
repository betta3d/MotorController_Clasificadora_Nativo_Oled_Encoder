#include "commands.h"
#include "globals.h"
#include "state.h"
#include "control.h"
#include "motion.h"
#include "eeprom_store.h"
#include "oled_ui.h"
#include "logger.h"
#include "homing.h"
#include "commands_status.h"
#include "commands_velocidades.h"
#include "commands_mecanica.h"
#include "commands_sectores.h"
#include "commands_control.h"


// Variable global para tiempo de inicio de rotación
uint32_t rotateStartMillis = 0;

using namespace App;

// Función auxiliar para normalizar comandos a mayúsculas
String normalizeCommand(const String& cmd) {
  String normalized = cmd;
  normalized.toUpperCase();
  return normalized;
}

void processCommands() {
  // ...existing code...
  if (!Serial.available()) return;
  
  String line = Serial.readStringUntil('\n'); 
  line.trim();
  
  // Normalizar comando para comparación (mayúsculas)
  String upperLine = normalizeCommand(line);
  float value = 0;

  // ===== COMANDO PARA VELOCIDAD DE HOMING =====
  if (upperLine.startsWith("V_HOME=")) {
    float vhome = upperLine.substring(7).toFloat();
    Comandos::procesarSetVelocidadHome("V_HOME", vhome);
    
  // ===== COMANDO TIEMPO ESTABILIZACION =====
  } else if (upperLine.startsWith("T_ESTAB=")) {
    uint32_t t_estab = upperLine.substring(8).toInt();
    Comandos::procesarSetTiempoEstabilizacion("T_ESTAB", t_estab);

  // ===== COMANDOS DE CONTROL =====
  } else if (upperLine.equals("SCURVE=ON") || upperLine.equals("SCURVE=OFF") || 
             upperLine.startsWith("ROTAR=") || upperLine.equals("STOP")) {
    Comandos::procesarComandoControl("CONTROL", upperLine);
    return;
    
  } else if (upperLine.equals("HOME")) {
    Comandos::procesarComandoHome("HOME");
    
  } else if (upperLine.startsWith("MASTER_DIR=")) {
    String dirStr = upperLine.substring(11);
    bool clockwise = (dirStr.equals("CW"));
    if (dirStr.equals("CW") || dirStr.equals("CCW")) {
      Comandos::procesarSetDireccionMaestra("MASTER_DIR", clockwise);
    } else {
      Serial.println("ERROR: Use MASTER_DIR=CW o MASTER_DIR=CCW");
    }
    
  } else if (upperLine.equals("STATUS")) {
    Comandos::procesarGetGeneralStatus();
    
  // ===== COMANDOS SECTORES ANGULARES =====
  } else if (upperLine.startsWith("DEG_LENTO_UP=")) {
    String rangeStr = upperLine.substring(13);
    Comandos::procesarSetSectorAngular("DEG_LENTO_UP", rangeStr);
    
  } else if (upperLine.startsWith("DEG_MEDIO=")) {
    String rangeStr = upperLine.substring(10);
    Comandos::procesarSetSectorAngular("DEG_MEDIO", rangeStr);
    
  } else if (upperLine.startsWith("DEG_LENTO_DOWN=")) {
    String rangeStr = upperLine.substring(15);
    Comandos::procesarSetSectorAngular("DEG_LENTO_DOWN", rangeStr);

  } else if (upperLine.startsWith("DEG_TRAVEL=")) {
    String rangeStr = upperLine.substring(11);
    Comandos::procesarSetSectorAngular("DEG_TRAVEL", rangeStr);
    
  // ===== COMANDOS CONFIGURACION EEPROM =====
  } else if (upperLine.startsWith("CM_PER_REV=")) {
    float value = upperLine.substring(11).toFloat();
    Comandos::procesarSetDistanciaPorRev("CM_PER_REV", value);
    
  } else if (upperLine.startsWith("V_SLOW=")) {
    float value = upperLine.substring(7).toFloat();
    Comandos::procesarSetVelocidadSistema("V_SLOW", value);
    
  } else if (upperLine.startsWith("V_MED=")) {
    float value = upperLine.substring(6).toFloat();
    Comandos::procesarSetVelocidadSistema("V_MED", value);
    
  } else if (upperLine.startsWith("V_FAST=")) {
    float value = upperLine.substring(7).toFloat();
    Comandos::procesarSetVelocidadSistema("V_FAST", value);
    
  } else if (upperLine.startsWith("ACCEL=")) {
    float value = upperLine.substring(6).toFloat();
    Comandos::procesarSetAceleracion("ACCEL", value);
    
  } else if (upperLine.startsWith("JERK=")) {
    float value = upperLine.substring(5).toFloat();
    Comandos::procesarSetJerk("JERK", value);
    
  // ===== COMANDOS PARAMETROS MECANICOS =====
  } else if (upperLine.startsWith("MOTOR_STEPS=")) {
    int steps = upperLine.substring(12).toInt();
    Comandos::procesarSetMotorSteps("MOTOR_STEPS", steps);
    
  } else if (upperLine.startsWith("MICROSTEPPING=")) {
    int micro = upperLine.substring(14).toInt();
    Comandos::procesarSetMicrostepping("MICROSTEPPING", micro);
    
  } else if (upperLine.startsWith("GEAR_RATIO=")) {
    float ratio = upperLine.substring(11).toFloat();
    Comandos::procesarSetGearRatio("GEAR_RATIO", ratio);
    
  // ===== COMANDOS PARAMETROS HOMING CENTRALIZADO (ya implementado DEG_OFFSET= anteriormente) =====
  
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