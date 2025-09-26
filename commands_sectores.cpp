#include "commands_sectores.h"
#include "globals.h"
#include <Arduino.h>
#include "eeprom_store.h"

using namespace App;

namespace Comandos {

// Función auxiliar para parsear rangos de sectores
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

bool procesarSetSectorAngular(const String& commandName, const String& rangeStr) {
    float start, end;
    bool wraps;
    
    if (!parseSectorRange(rangeStr, start, end, wraps)) {
        Serial.printf("ERROR: Formato incorrecto. Use %s=start-end (ej: 350-10)\n", commandName.c_str());
        return false;
    }

    bool success = false;
    if (commandName == "DEG_LENTO_UP") {
        DEG_LENTO_UP.start = start;
        DEG_LENTO_UP.end = end;
        DEG_LENTO_UP.wraps = wraps;
        success = true;
    } else if (commandName == "DEG_MEDIO") {
        DEG_MEDIO.start = start;
        DEG_MEDIO.end = end;
        DEG_MEDIO.wraps = wraps;
        success = true;
    } else if (commandName == "DEG_LENTO_DOWN") {
        DEG_LENTO_DOWN.start = start;
        DEG_LENTO_DOWN.end = end;
        DEG_LENTO_DOWN.wraps = wraps;
        success = true;
    } else if (commandName == "DEG_TRAVEL") {
        DEG_TRAVEL.start = start;
        DEG_TRAVEL.end = end;
        DEG_TRAVEL.wraps = wraps;
        success = true;
    }

    if (success) {
        saveConfig();
        Serial.printf("%s actualizado: %.0f-%.0f%s\n", 
                     commandName.c_str(), start, end, wraps ? " (wrap)" : "");
        return true;
    }
    return false;
}

} // namespace Comandos