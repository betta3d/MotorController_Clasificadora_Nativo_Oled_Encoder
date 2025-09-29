#pragma once
#include <Arduino.h>

namespace Buzzer {
  // Configuración (ajustable) del beep de navegación
  constexpr uint16_t BUZZER_NAV_FREQ = 3800; // Hz
  constexpr uint16_t BUZZER_NAV_MS   = 50;   // Duración ms (fluidez solicitada)
  // Inicializa pin del buzzer (llamar en setup)
  void init();
  // Pip corto: navegación / feedback ligero
  void beepNav();
  // Pip largo: guardado EEPROM o acción importante
  void beepSave();
  // Beep al retroceder (más agudo)
  void beepBack();
  // Beep genérico configurable (bloqueante)
  void beep(uint16_t freqHz, uint16_t ms, uint8_t volume = 255);
  // Actualización no bloqueante para beepNav (apaga automáticamente)
  void update();
  // Melody de arranque (tres notas tipo DJI-lite) no bloqueante
  void startStartupMelody();
  // Opción futura: cola no bloqueante
}
