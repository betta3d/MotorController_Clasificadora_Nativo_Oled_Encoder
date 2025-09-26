#ifndef COMMANDS_CONTROL_H
#define COMMANDS_CONTROL_H

#include <Arduino.h>

namespace Comandos {

/**
 * Procesa comandos de control del sistema (SCURVE, ROTAR, STOP)
 * @param commandName Nombre del comando
 * @param value Valor del comando (para ROTAR)
 * @param upperLine Línea completa del comando para procesamiento especial
 * @return true si se procesó correctamente
 */
bool procesarComandoControl(const String& commandName, const String& upperLine);

/**
 * Procesa comando de configuración de dirección maestra
 * @param commandName Nombre del comando
 * @param clockwise true para CW, false para CCW
 * @return true si se procesó correctamente
 */
bool procesarSetDireccionMaestra(const String& commandName, bool clockwise);

/**
 * Procesa comando de homing manual
 * @param commandName Nombre del comando
 * @return true si se procesó correctamente
 */
bool procesarComandoHome(const String& commandName);

/**
 * Ejecuta rotación pendiente después de completar homing
 * @return true si se ejecutó rotación pendiente, false si no había ninguna
 */
bool ejecutarRotacionPendiente();

} // namespace Comandos

#endif // COMMANDS_CONTROL_H