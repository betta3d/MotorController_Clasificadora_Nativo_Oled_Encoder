#pragma once
#include <stdint.h>

namespace App {
  // Inicializa el encoder (decodificación por software + botón)
  void encoderInit();

  // Devuelve el delta de "detents" desde la última llamada.
  // Normaliza a 1 paso por “clic” mecánico.
  int8_t encoderReadDelta();

  // Lectura de click del pulsador integrado (con debounce)
  bool encoderReadClick();

  // Estado actual crudo (true si presionado) sin debounce para long-press
  bool encoderButtonPressedRaw();
}
