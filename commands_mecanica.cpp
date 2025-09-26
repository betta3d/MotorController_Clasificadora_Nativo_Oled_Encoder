#include "commands_mecanica.h"
#include "globals.h"
#include "logger.h"
#include "eeprom_store.h"
#include <Arduino.h>

using namespace App;

namespace Comandos {

bool procesarSetMotorSteps(const String& commandName, uint32_t newValue) {
    if (newValue > 0) {
        MOTOR_FULL_STEPS_PER_REV = newValue;
        stepsPerRev = (uint32_t)(MOTOR_FULL_STEPS_PER_REV * MICROSTEPPING * GEAR_RATIO);
        saveConfig();
        Serial.printf("MOTOR_STEPS actualizado: %lu steps/rev\n", newValue);
        return true;
    } else {
        logPrint("ERROR", "MOTOR_STEPS debe ser > 0");
        return false;
    }
}

bool procesarSetMicrostepping(const String& commandName, uint32_t newValue) {
    if (newValue > 0) {
        MICROSTEPPING = newValue;
        stepsPerRev = (uint32_t)(MOTOR_FULL_STEPS_PER_REV * MICROSTEPPING * GEAR_RATIO);
        saveConfig();
        Serial.printf("MICROSTEPPING actualizado: %lu\n", newValue);
        return true;
    } else {
        Serial.println("ERROR: MICROSTEPPING debe ser > 0");
        return false;
    }
}

bool procesarSetGearRatio(const String& commandName, float newValue) {
    if (newValue > 0) {
        GEAR_RATIO = newValue;
        stepsPerRev = (uint32_t)(MOTOR_FULL_STEPS_PER_REV * MICROSTEPPING * GEAR_RATIO);
        saveConfig();
        Serial.printf("GEAR_RATIO actualizado: %.2f\n", newValue);
        return true;
    } else {
        Serial.println("ERROR: GEAR_RATIO debe ser > 0");
        return false;
    }
}

} // namespace Comandos