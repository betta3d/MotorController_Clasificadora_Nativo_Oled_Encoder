#ifndef COMMANDS_MECANICA_H
#define COMMANDS_MECANICA_H

#include <Arduino.h>

namespace Comandos {

/**
 * Procesa comando de pasos del motor por revolución
 * @param commandName Nombre del comando
 * @param newValue Nuevo valor de pasos por revolución
 * @return true si se procesó correctamente
 */
bool procesarSetMotorSteps(const String& commandName, uint32_t newValue);

/**
 * Procesa comando de microstepping
 * @param commandName Nombre del comando
 * @param newValue Nuevo valor de microstepping
 * @return true si se procesó correctamente
 */
bool procesarSetMicrostepping(const String& commandName, uint32_t newValue);

/**
 * Procesa comando de relación de engranajes
 * @param commandName Nombre del comando
 * @param newValue Nuevo valor de gear ratio
 * @return true si se procesó correctamente
 */
bool procesarSetGearRatio(const String& commandName, float newValue);

} // namespace Comandos

#endif // COMMANDS_MECANICA_H