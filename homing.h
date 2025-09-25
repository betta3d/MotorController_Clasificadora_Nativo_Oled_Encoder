#pragma once
#include <Arduino.h>

namespace App {

// Variables para proceso homing avanzado (ROTAR)
extern volatile int32_t rotarHomingStepsCounter;  // Contador de pasos en proceso homing
extern volatile int32_t rotarHomingOffsetCounter; // Contador de pasos para offset
extern volatile uint32_t rotarHomingStartTime;    // Tiempo inicio estabilización
extern volatile bool rotarHomingFoundSensor;     // Flag cuando se encuentra sensor

// Funciones para homing avanzado de ROTAR
void initRotarHoming();           // Inicializa proceso homing para ROTAR
void processRotarHomingSeek();    // Procesa búsqueda del sensor
void processRotarHomingOffset();  // Procesa aplicación del offset
void processRotarHomingStabilize(); // Procesa tiempo de estabilización
bool rotarHomingSeekCompleted();  // Verifica si completó búsqueda
bool rotarHomingOffsetCompleted(); // Verifica si completó offset
bool rotarHomingStabilizeCompleted(); // Verifica si completó estabilización

} // namespace App