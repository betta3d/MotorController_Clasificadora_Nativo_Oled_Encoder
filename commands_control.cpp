#include "commands_control.h"
#include "globals.h"
#include "state.h"
#include "eeprom_store.h"
#include "logger.h"
#include "homing.h"
#include "motion.h"
#include "oled_ui.h"
#include <Arduino.h>

using namespace App;

// Variable global para tiempo de inicio de rotación
extern uint32_t rotateStartMillis;

namespace Comandos {

bool procesarComandoControl(const String& commandName, const String& upperLine) {
    if (upperLine.equals("SCURVE=ON")) {
        Cfg.enable_s_curve = true;
        saveConfig();
        logPrint("CONFIG", "S-CURVE=ON (rampas suaves habilitadas)");
        return true;
        
    } else if (upperLine.equals("SCURVE=OFF")) {
        Cfg.enable_s_curve = false;
        saveConfig();
        logPrint("CONFIG", "S-CURVE=OFF (control directo de velocidad)");
        return true;
        
    } else if (upperLine.startsWith("ROTAR=")) {
        float value = upperLine.substring(6).toFloat();
        if (value != 0) {
            // Si está en RUNNING, primero detener
            if (state == SysState::RUNNING) {
                state = SysState::STOPPING;
                logPrint("ROTAR", "Deteniendo RUNNING primero...");
                delay(100);
            }

            // Verificar si ya está en homing o ya está homed
            if (state == SysState::HOMING_SEEK) {
                logPrint("ROTAR", "Homing ya en progreso, esperando...");
                return false;
            }
            
            // Si no está homed, iniciar homing centralizado
            if (!homed) {
                logPrint("ROTAR", "Iniciando homing antes de rotar...");
                pendingRotateRevs = value; // Guardar comando para después del homing
                App::startCentralizedHoming();
                return true; // Retornar para que el loop principal maneje el homing
            }

            // Si ya está homed, proceder con la rotación
            // Configurar rotación
            applyConfigToProfiles();
            rotateTargetRevs = abs(value); // Solo usar valor absoluto para vueltas
            rotateTargetSteps = (int32_t)(abs(value) * (float)stepsPerRev);
            rotateDirection = (value > 0); // Solo para conteo, no para dirección física
            rotateStepsCounter = 0;
            rotateMode = true;
            
            // Configurar dirección basada en signo y configuración master
            // Selector: true=usar maestra, false=usar inversa
            setDirection(value > 0);

            logPrint("DEBUG", "ROTAR iniciado - revisa físicamente y usa STOP cuando complete las vueltas reales");
            logPrintf("DEBUG", "stepsPerRev=%lu, value=%.1f, rotateTargetSteps=%ld", 
                     stepsPerRev, value, (long)rotateTargetSteps);
            logPrintf("DEBUG", "Observa cuándo complete %.1f vueltas físicas y usa STOP", value);

            // Iniciar rotación
            rotateStartMillis = millis();
            state = SysState::ROTATING;
            uiScreen = UiScreen::STATUS;
            screensaverActive = false;
            screensaverStartTime = rotateStartMillis;

            Serial.printf("[ROTAR] Iniciando %.1f vueltas (%s) - %ld pasos objetivo\n", 
                         value, (value > 0) ? "CW" : "CCW", (long)rotateTargetSteps);
            return true;
        } else {
            logPrint("ERROR", "ROTAR requiere valor diferente de 0");
            return false;
        }
        
    } else if (upperLine.equals("STOP")) {
        if (state == SysState::ROTATING) {
            // Mostrar pasos reales ejecutados antes de resetear
            float realRevs = (float)abs(rotateStepsCounter) / (float)stepsPerRev;
            float realDegrees = (float)abs(rotateStepsCounter) * degPerStep();
            uint32_t rotateEndMillis = millis();
            uint32_t rotateDurationMs = (rotateEndMillis > rotateStartMillis) ? 
                                       (rotateEndMillis - rotateStartMillis) : 0;
            float rotateDurationSec = rotateDurationMs / 1000.0f;
            float avgSpeedRPS = (rotateDurationSec > 0) ? (realRevs / rotateDurationSec) : 0;
            
            Serial.printf("[STOP] ROTACION DETENIDA - Pasos ejecutados: %ld (%.2f vueltas, %.1f°)\n", 
                         (long)abs(rotateStepsCounter), realRevs, realDegrees);
            Serial.printf("[ROTAR] Tiempo: %.2f s | Velocidad promedio: %.2f vueltas/s\n", 
                         rotateDurationSec, avgSpeedRPS);
            
            if (rotateTargetRevs != 0) {
                float factor = realRevs / rotateTargetRevs;
                Serial.printf("[CALIBRACION] Factor real: %.3f (usar MICROSTEPPING=%.0f si es diferente a 16)\n", 
                             factor, 16.0f * factor);
            }
            state = SysState::STOPPING;
            rotateMode = false;
            rotateStepsCounter = 0;
            return true;
            
        } else if (state == SysState::RUNNING) {
            state = SysState::STOPPING;
            Serial.println("[STOP] MOTOR DETENIENDO");
            return true;
        } else {
            Serial.println("[STOP] Motor ya detenido");
            return true;
        }
    }
    return false;
}

bool procesarSetDireccionMaestra(const String& commandName, bool clockwise) {
    master_direction = clockwise;
    inverse_direction = !master_direction;
    saveConfig();
    
    Serial.printf("%s actualizado: %s (inverse: %s)\n", 
                  commandName.c_str(),
                  master_direction ? "CW" : "CCW",
                  inverse_direction ? "CW" : "CCW");
    
    logPrintf("CONFIG", "Dirección maestra: %s", master_direction ? "CW" : "CCW");
    return true;
}

bool procesarComandoHome(const String& commandName) {
    // Verificar que no esté ya en proceso de homing
    if (state == SysState::HOMING_SEEK) {
        Serial.println("[HOME] Homing ya en progreso");
        return false;
    }
    
    // Si está en movimiento, detener primero
    if (state == SysState::RUNNING || state == SysState::ROTATING) {
        state = SysState::STOPPING;
        logPrint("HOME", "Deteniendo movimiento antes de iniciar homing");
        delay(100);
    }
    
    // Iniciar homing centralizado
    App::startCentralizedHoming();
    Serial.println("[HOME] Iniciando proceso de homing...");
    logPrint("HOME", "Comando HOME ejecutado - iniciando homing centralizado");
    
    return true;
}

bool ejecutarRotacionPendiente() {
    if (pendingRotateRevs == 0.0f) {
        return false; // No hay rotación pendiente
    }
    
    float value = pendingRotateRevs;
    pendingRotateRevs = 0.0f; // Limpiar rotación pendiente
    
    // Configurar rotación
    applyConfigToProfiles();
    rotateTargetRevs = abs(value); // Solo usar valor absoluto para vueltas
    rotateTargetSteps = (int32_t)(abs(value) * (float)stepsPerRev);
    rotateDirection = (value > 0); // Solo para conteo, no para dirección física
    rotateStepsCounter = 0;
    rotateMode = true;
    
    // Configurar dirección basada en signo y configuración master
    // Selector: true=usar maestra, false=usar inversa
    setDirection(value > 0);

    logPrint("DEBUG", "ROTAR pendiente iniciado - revisa físicamente y usa STOP cuando complete las vueltas reales");
    logPrintf("DEBUG", "stepsPerRev=%lu, value=%.1f, rotateTargetSteps=%ld", 
             stepsPerRev, value, (long)rotateTargetSteps);
    logPrintf("DEBUG", "Observa cuándo complete %.1f vueltas físicas y usa STOP", value);

    // Iniciar rotación
    rotateStartMillis = millis();
    state = SysState::ROTATING;
    uiScreen = UiScreen::STATUS;
    screensaverActive = false;
    screensaverStartTime = rotateStartMillis;

    Serial.printf("[ROTAR] Iniciando %.1f vueltas (%s) - %ld pasos objetivo (tras homing)\n", 
                 value, (value > 0) ? "CW" : "CCW", (long)rotateTargetSteps);
    return true;
}

} // namespace Comandos