#pragma once
// Referencia de clasificación de huevos (NCh1376.Of78) provista por el usuario.
// Se usará para plantillas de configuración de estaciones de pesaje.
// Las estaciones podrán mapear sus rangos personalizados; esto solo sirve de guía.

namespace App {
struct EggWeightClass { const char* nombre; float minG; float maxG; }; // maxG exclusivo salvo MENOS_DE

// Rango especial: Sobre 68 gramos (sin límite superior práctico aquí, usar 200g como techo razonable)
static constexpr EggWeightClass EGG_WEIGHT_CLASSES[] = {
  {"SuperExtra", 68.0f, 200.0f},   // >68
  {"Extra",      61.0f, 68.0f},    // 61–<68
  {"Grande",     54.0f, 61.0f},    // 54–<61
  {"Mediano",    47.0f, 54.0f},    // 47–<54
  {"Chico",      40.0f, 47.0f},    // 40–<47
  {"MuyChico",    0.0f, 40.0f},    // <40
};
static constexpr int EGG_WEIGHT_CLASS_COUNT = sizeof(EGG_WEIGHT_CLASSES)/sizeof(EggWeightClass);
}
