#ifndef COMMANDS_STATUS_H
#define COMMANDS_STATUS_H

#include <Arduino.h>

namespace Comandos {

/**
 * Procesa y muestra el estado general del sistema
 * Incluye configuración, parámetros mecánicos, sectores angulares,
 * homing, estado cinemático actual y comandos principales
 */
void procesarGetGeneralStatus();

} // namespace Comandos

#endif // COMMANDS_STATUS_H