#ifndef COMMANDS_VELOCIDADES_H
#define COMMANDS_VELOCIDADES_H

#include <Arduino.h>

namespace Comandos {

/**
 * Procesa comando de velocidad de homing
 * @param commandName Nombre del comando (para logs)
 * @param newValue Nuevo valor de velocidad en cm/s
 * @return true si se procesó correctamente
 */
bool procesarSetVelocidadHome(const String& commandName, float newValue);

/**
 * Procesa comando de tiempo de estabilización de homing
 * @param commandName Nombre del comando (para logs)
 * @param newValue Nuevo valor en milisegundos
 * @return true si se procesó correctamente
 */
bool procesarSetTiempoEstabilizacion(const String& commandName, uint32_t newValue);

/**
 * Procesa comandos de velocidades del sistema (V_SLOW, V_MED, V_FAST)
 * @param commandName Nombre del comando
 * @param newValue Nuevo valor de velocidad en cm/s
 * @return true si se procesó correctamente
 */
bool procesarSetVelocidadSistema(const String& commandName, float newValue);

/**
 * Procesa comando de aceleración
 * @param commandName Nombre del comando
 * @param newValue Nuevo valor de aceleración en cm/s²
 * @return true si se procesó correctamente
 */
bool procesarSetAceleracion(const String& commandName, float newValue);

/**
 * Procesa comando de jerk
 * @param commandName Nombre del comando
 * @param newValue Nuevo valor de jerk en cm/s³
 * @return true si se procesó correctamente
 */
bool procesarSetJerk(const String& commandName, float newValue);

/**
 * Procesa comando de distancia por revolución
 * @param commandName Nombre del comando
 * @param newValue Nuevo valor en cm/rev
 * @return true si se procesó correctamente
 */
bool procesarSetDistanciaPorRev(const String& commandName, float newValue);

} // namespace Comandos

#endif // COMMANDS_VELOCIDADES_H