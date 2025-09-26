#ifndef COMMANDS_SECTORES_H
#define COMMANDS_SECTORES_H

#include <Arduino.h>

namespace Comandos {

/**
 * Procesa comandos de configuración de sectores angulares
 * @param commandName Nombre del comando (DEG_LENTO_UP, DEG_MEDIO, etc.)
 * @param rangeStr String con el rango en formato "start-end"
 * @return true si se procesó correctamente
 */
bool procesarSetSectorAngular(const String& commandName, const String& rangeStr);

} // namespace Comandos

#endif // COMMANDS_SECTORES_H