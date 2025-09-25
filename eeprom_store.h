#pragma once
#include <Arduino.h>

namespace App {
  extern const uint32_t CFG_MAGIC; // definido en .cpp
  bool loadConfig();
  void saveConfig();
  void setDefaults();
  uint32_t simpleCRC(const uint8_t* p, size_t n);
} // namespace App
