#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

/**
 * @brief Sistema de logging con categorías configurables
 * 
 * Permite controlar qué categorías de logs se muestran mediante comandos serie
 * LOG-[CATEGORIA]=ON/OFF y variables booleanas de configuración.
 */

namespace App {

// ===== CATEGORÍAS DE LOGGING DISPONIBLES =====
extern bool LOG_SYSTEM;       // Mensajes del sistema, inicialización
extern bool LOG_HOME;         // Proceso de homing
extern bool LOG_ROTAR;        // Comandos de rotación  
extern bool LOG_START_STOP;   // Botones físicos START/STOP
extern bool LOG_TELEMETRIA;   // Estados del sistema FSM
extern bool LOG_DEBUG;        // Información de depuración
extern bool LOG_CONFIG;       // Cambios de configuración
extern bool LOG_ERROR;        // Mensajes de error
extern bool LOG_UI;           // Interfaz usuario OLED/Encoder
extern bool LOG_RUN;          // Modo RUNNING continuo
extern bool LOG_WARNING;      // Advertencias del sistema
extern bool LOG_CALIBRACION;  // Factores de corrección
extern bool LOG_WIFI;         // Conectividad WiFi
extern bool LOG_ALL;          // Control global (master switch)

/**
 * @brief Imprime mensaje con categoría si está habilitada
 * @param category Categoría del log (sin corchetes)
 * @param message Mensaje a imprimir
 */
void logPrint(const char* category, const String& message);

/**
 * @brief Versión printf del logger
 * @param category Categoría del log (sin corchetes)
 * @param format String de formato estilo printf
 * @param ... Argumentos para el formato
 */
void logPrintf(const char* category, const char* format, ...);

/**
 * @brief Habilita/deshabilita una categoría de log
 * @param category Nombre de la categoría (ej: "HOME", "DEBUG")
 * @param enabled true para habilitar, false para deshabilitar
 * @return true si la categoría existe, false si no se encontró
 */
bool setLogEnabled(const String& category, bool enabled);

/**
 * @brief Obtiene el estado de una categoría de log
 * @param category Nombre de la categoría
 * @return true si está habilitada, false si no
 */
bool isLogEnabled(const String& category);

/**
 * @brief Muestra el estado de todas las categorías de log
 */
void showLogStatus();

/**
 * @brief Inicializa el sistema de logging con valores por defecto
 */
void initLogging();

}

#endif // LOGGER_H