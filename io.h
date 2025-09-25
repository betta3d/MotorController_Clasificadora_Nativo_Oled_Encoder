#pragma once
#include "state.h"

namespace App {
  void ioInit();                         // pines, I2C, LEDs
  void updateLeds(SysState st, bool homedNow);
} // namespace App
