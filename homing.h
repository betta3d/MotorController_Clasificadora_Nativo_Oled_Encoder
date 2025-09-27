#pragma once
#include <Arduino.h>
#include <cstdint>

namespace App {

enum class HomingPhase : uint8_t {
    SEEK = 0,
    OFFSET = 1,
    STABILIZE = 2,
    DONE = 3,
    FAULT = 4
};

struct HomingContext {
    HomingPhase phase;          // Fase actual
    int64_t baselineSteps;      // Base para medir delta en fase actual
    bool sensorFound;           // Bandera sensor ya detectado
    uint32_t stabilizeStartMs;  // Tiempo de inicio de estabilización
    bool triedAlternate;        // Ya probamos dirección alterna
    int64_t firstBaselineSteps; // Base inicial para medir recorrido total (ambas direcciones)
    bool initialSelector;       // Selector de dirección usado al iniciar (true=master, false=inverse)
};

extern HomingContext homingCtx;

void startCentralizedHoming();
void processCentralizedHoming();
bool centralizedHomingCompleted();

} // namespace App