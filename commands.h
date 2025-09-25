#ifndef COMMANDS_H
#define COMMANDS_H

#include <Arduino.h>

/**
 * @brief Procesa los comandos seriales recibidos
 * 
 * Esta función se encarga de:
 * - Leer comandos del puerto serie
 * - Procesar comandos tanto en mayúsculas como minúsculas
 * - Ejecutar las acciones correspondientes
 * - Guardar configuraciones en EEPROM cuando sea necesario
 */
void processCommands();

/**
 * @brief Función auxiliar para parsear rangos de sectores
 * @param rangeStr String con formato "start-end" (ej: "355-10")
 * @param start Referencia donde se almacenará el valor inicial
 * @param end Referencia donde se almacenará el valor final  
 * @param wraps Referencia que indica si el sector cruza 0° (wrap)
 * @return true si el parseo fue exitoso, false en caso contrario
 */
bool parseSectorRange(const String& rangeStr, float& start, float& end, bool& wraps);

#endif // COMMANDS_H