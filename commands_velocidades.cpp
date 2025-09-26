#include "commands_velocidades.h"
#include "globals.h"
#include "eeprom_store.h"
#include "motion.h"
#include <Arduino.h>

using namespace App;

namespace Comandos {

bool procesarSetVelocidadHome(const String& commandName, float newValue) {
    if (newValue > 0.0f) {
        V_HOME_CMPS = newValue;
        saveConfig();
        Serial.printf("V_HOME_CMPS actualizado: %.2f cm/s\n", V_HOME_CMPS);
        return true;
    } else {
        Serial.println("ERROR: V_HOME debe ser mayor a 0. Ejemplo: V_HOME=8.5");
        return false;
    }
}

bool procesarSetTiempoEstabilizacion(const String& commandName, uint32_t newValue) {
    if (newValue >= 500 && newValue <= 10000) {
        TIEMPO_ESTABILIZACION_HOME = newValue;
        saveConfig();
        Serial.printf("TIEMPO_ESTABILIZACION_HOME actualizado: %lu ms\n", TIEMPO_ESTABILIZACION_HOME);
        return true;
    } else {
        Serial.println("ERROR: T_ESTAB debe estar entre 500 y 10000 ms. Ejemplo: T_ESTAB=2000");
        return false;
    }
}

bool procesarSetVelocidadSistema(const String& commandName, float newValue) {
    if (newValue <= 0) {
        Serial.printf("ERROR: %s debe ser > 0\n", commandName.c_str());
        return false;
    }

    bool success = false;
    if (commandName == "V_SLOW") {
        Cfg.v_slow_cmps = newValue;
        success = true;
    } else if (commandName == "V_MED") {
        Cfg.v_med_cmps = newValue;
        success = true;
    } else if (commandName == "V_FAST") {
        Cfg.v_fast_cmps = newValue;
        success = true;
    }

    if (success) {
        saveConfig();
        applyConfigToProfiles();
        Serial.printf("%s actualizado: %.1f cm/s\n", commandName.c_str(), newValue);
        return true;
    }
    return false;
}

bool procesarSetAceleracion(const String& commandName, float newValue) {
    if (newValue > 0) {
        Cfg.accel_cmps2 = newValue;
        saveConfig();
        applyConfigToProfiles();
        Serial.printf("ACCEL actualizado: %.1f cm/s²\n", newValue);
        return true;
    } else {
        Serial.println("ERROR: ACCEL debe ser > 0");
        return false;
    }
}

bool procesarSetJerk(const String& commandName, float newValue) {
    if (newValue > 0) {
        Cfg.jerk_cmps3 = newValue;
        saveConfig();
        applyConfigToProfiles();
        Serial.printf("JERK actualizado: %.1f cm/s³\n", newValue);
        return true;
    } else {
        Serial.println("ERROR: JERK debe ser > 0");
        return false;
    }
}

bool procesarSetDistanciaPorRev(const String& commandName, float newValue) {
    if (newValue > 0) {
        Cfg.cm_per_rev = newValue;
        saveConfig();
        applyConfigToProfiles();
        Serial.printf("CM_PER_REV actualizado: %.2f cm/rev\n", newValue);
        return true;
    } else {
        Serial.println("ERROR: CM_PER_REV debe ser > 0");
        return false;
    }
}

} // namespace Comandos