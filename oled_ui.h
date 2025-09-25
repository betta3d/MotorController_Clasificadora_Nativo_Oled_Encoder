#pragma once
#include <U8g2lib.h>
#include "state.h"

namespace App {
  // Instancia global del display (definida en .cpp)
  extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

  // Screensaver variables
  extern bool screensaverActive;
  extern unsigned long screensaverStartTime;

  // Inicialización del OLED
  void oledInit();

  // Procesa la UI con el encoder (delta y click)
  void uiProcess(int8_t encDelta, bool encClick);

  // Dibuja la pantalla actual (STATUS / MENU / EDIT)
  void uiRender();

} // namespace App
