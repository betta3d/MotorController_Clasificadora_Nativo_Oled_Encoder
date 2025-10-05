#include "logger.h"
#include <stdarg.h>

namespace App {

// ===== VARIABLES DE CONTROL DE LOGGING =====
bool LOG_SYSTEM = true;        // Mensajes del sistema - habilitado por defecto
bool LOG_HOME = true;          // Proceso de homing
bool LOG_ROTAR = true;         // Comandos de rotación
bool LOG_START_STOP = true;    // Botones físicos
bool LOG_TELEMETRIA = false;   // Estados FSM - deshabilitado por defecto (muy verboso)
bool LOG_DEBUG = false;        // Debug info - deshabilitado por defecto
bool LOG_CONFIG = true;        // Cambios de configuración
bool LOG_ERROR = true;         // Errores - siempre habilitado por defecto
bool LOG_UI = true;            // Interfaz usuario
bool LOG_RUN = true;           // Modo RUNNING
bool LOG_WARNING = true;       // Advertencias
bool LOG_CALIBRACION = true;   // Factores de corrección
bool LOG_WIFI = true;          // Conectividad WiFi
bool LOG_ALL = true;           // Control global - master switch
bool LOG_PLANNER = true;       // Planner diagnostics
bool LOG_SERVO = true;         // Servo diagnostics

// ===== IMPLEMENTACIÓN FUNCIONES =====

void logPrint(const char* category, const String& message) {
  // Si LOG_ALL está deshabilitado, no mostrar nada
  if (!LOG_ALL) return;
  
  // Verificar si la categoría específica está habilitada
  if (!isLogEnabled(category)) return;
  
  // Imprimir con formato [CATEGORIA] mensaje
  Serial.print("[");
  Serial.print(category);
  Serial.print("] ");
  Serial.println(message);
}

void logPrintf(const char* category, const char* format, ...) {
  // Si LOG_ALL está deshabilitado, no mostrar nada
  if (!LOG_ALL) return;
  
  // Verificar si la categoría específica está habilitada
  if (!isLogEnabled(category)) return;
  
  // Buffer para el mensaje formateado
  char buffer[512];
  
  // Formatear mensaje con argumentos variables
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  
  // Imprimir con formato [CATEGORIA] mensaje
  Serial.print("[");
  Serial.print(category);
  Serial.print("] ");
  Serial.println(buffer);
}

bool setLogEnabled(const String& category, bool enabled) {
  String cat = category;
  cat.toUpperCase(); // Normalizar a mayúsculas
  
  if (cat == "SYSTEM") {
    LOG_SYSTEM = enabled;
  } else if (cat == "HOME") {
    LOG_HOME = enabled;
  } else if (cat == "ROTAR") {
    LOG_ROTAR = enabled;
  } else if (cat == "START_STOP" || cat == "START/STOP") {
    LOG_START_STOP = enabled;
  } else if (cat == "TELEMETRIA") {
    LOG_TELEMETRIA = enabled;
  } else if (cat == "DEBUG") {
    LOG_DEBUG = enabled;
  } else if (cat == "CONFIG") {
    LOG_CONFIG = enabled;
  } else if (cat == "ERROR") {
    LOG_ERROR = enabled;
  } else if (cat == "UI") {
    LOG_UI = enabled;
  } else if (cat == "RUN") {
    LOG_RUN = enabled;
  } else if (cat == "WARNING") {
    LOG_WARNING = enabled;
  } else if (cat == "CALIBRACION") {
    LOG_CALIBRACION = enabled;
  } else if (cat == "WIFI") {
    LOG_WIFI = enabled;
  } else if (cat == "PLANNER" || cat == "PLNQ" || cat == "PLNS" || cat == "CTRL") {
    LOG_PLANNER = enabled; // Treat specific planner sub-tags as PLANNER umbrella
  } else if (cat == "SERVO") {
    LOG_SERVO = enabled;
  } else if (cat == "ALL") {
    LOG_ALL = enabled;
  } else {
    return false; // Categoría no encontrada
  }
  
  return true;
}

bool isLogEnabled(const String& category) {
  String cat = category;
  cat.toUpperCase(); // Normalizar a mayúsculas
  
  if (cat == "SYSTEM") return LOG_SYSTEM;
  if (cat == "HOME") return LOG_HOME;
  if (cat == "ROTAR") return LOG_ROTAR;
  if (cat == "START_STOP" || cat == "START/STOP") return LOG_START_STOP;
  if (cat == "TELEMETRIA") return LOG_TELEMETRIA;
  if (cat == "DEBUG") return LOG_DEBUG;
  if (cat == "CONFIG") return LOG_CONFIG;
  if (cat == "ERROR") return LOG_ERROR;
  if (cat == "UI") return LOG_UI;
  if (cat == "RUN") return LOG_RUN;
  if (cat == "WARNING") return LOG_WARNING;
  if (cat == "CALIBRACION") return LOG_CALIBRACION;
  if (cat == "WIFI") return LOG_WIFI;
  if (cat == "PLANNER" || cat == "PLNQ" || cat == "PLNS" || cat == "CTRL") return LOG_PLANNER;
  if (cat == "SERVO") return LOG_SERVO;
  if (cat == "ALL") return LOG_ALL;
  
  return false; // Categoría no encontrada
}

void showLogStatus() {
  Serial.println("\n=== ESTADO SISTEMA LOGGING ===");
  Serial.printf("LOG_ALL: %s (Control maestro - si OFF, deshabilita todo)\n", LOG_ALL ? "ON" : "OFF");
  Serial.println("\n--- Categorías individuales ---");
  Serial.printf("LOG_SYSTEM: %s      - Mensajes del sistema, inicialización\n", LOG_SYSTEM ? "ON" : "OFF");
  Serial.printf("LOG_HOME: %s        - Proceso de homing automático\n", LOG_HOME ? "ON" : "OFF");
  Serial.printf("LOG_ROTAR: %s       - Comandos rotación ROTAR=N\n", LOG_ROTAR ? "ON" : "OFF");
  Serial.printf("LOG_START_STOP: %s  - Botones físicos START/STOP\n", LOG_START_STOP ? "ON" : "OFF");
  Serial.printf("LOG_TELEMETRIA: %s  - Estados FSM y telemetría continua\n", LOG_TELEMETRIA ? "ON" : "OFF");
  Serial.printf("LOG_DEBUG: %s       - Información detallada de depuración\n", LOG_DEBUG ? "ON" : "OFF");
  Serial.printf("LOG_CONFIG: %s      - Cambios de configuración EEPROM\n", LOG_CONFIG ? "ON" : "OFF");
  Serial.printf("LOG_ERROR: %s       - Mensajes de error del sistema\n", LOG_ERROR ? "ON" : "OFF");
  Serial.printf("LOG_UI: %s          - Interfaz OLED y encoder\n", LOG_UI ? "ON" : "OFF");
  Serial.printf("LOG_RUN: %s         - Modo RUNNING continuo\n", LOG_RUN ? "ON" : "OFF");
  Serial.printf("LOG_WARNING: %s     - Advertencias del sistema\n", LOG_WARNING ? "ON" : "OFF");
  Serial.printf("LOG_CALIBRACION: %s - Factores corrección y calibración\n", LOG_CALIBRACION ? "ON" : "OFF");
  Serial.printf("LOG_WIFI: %s        - Conectividad WiFi (scan/conexión)\n", LOG_WIFI ? "ON" : "OFF");
  Serial.printf("LOG_PLANNER: %s     - Diagnósticos del planificador (PLNQ/PLNS/CTRL)\n", LOG_PLANNER ? "ON" : "OFF");
  Serial.printf("LOG_SERVO: %s       - Diagnósticos del servo SG90\n", LOG_SERVO ? "ON" : "OFF");
  
  Serial.println("\n--- Comandos disponibles ---");
  Serial.println("LOG-[CATEGORIA]=ON/OFF  - Controla categoría específica");
  Serial.println("LOG-ALL=OFF             - Deshabilita TODOS los logs");
  Serial.println("LOG-STATUS              - Muestra este estado");
  Serial.println("\nEjemplos:");
  Serial.println("  LOG-DEBUG=ON    - Habilita logs de debug");
  Serial.println("  LOG-HOME=OFF    - Deshabilita logs de homing");
  Serial.println("  LOG-ALL=OFF     - Silencia todo el sistema");
}

void initLogging() {
  // Valores por defecto - ya están definidos arriba
  // Esta función puede usarse para reinicializar o cargar desde EEPROM
  
  logPrint("SYSTEM", "Sistema de logging inicializado");
  logPrint("SYSTEM", "Use LOG-STATUS para ver configuración actual");
}

}