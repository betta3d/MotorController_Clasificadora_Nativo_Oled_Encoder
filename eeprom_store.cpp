#include "eeprom_store.h"
#include "globals.h"
#include <EEPROM.h>

namespace App {

static constexpr size_t   EEPROM_SIZE = 512;
static constexpr int      EEPROM_ADDR = 0;
const  uint32_t CFG_MAGIC = 0x4F4C4533; // 'OLE3'

uint32_t simpleCRC(const uint8_t* p, size_t n) {
  uint32_t c = 0xA5A5A5A5u;
  for (size_t i=0; i<n; ++i) c = (c << 1) ^ (c >> 31) ^ p[i];
  return c;
}

void setDefaults() {
  Cfg.magic        = CFG_MAGIC;
  Cfg.cm_per_rev   = 20.0f;
  Cfg.v_slow_cmps  = 10.0f;
  Cfg.v_med_cmps   = 30.0f;
  Cfg.v_fast_cmps  = 90.0f;
  Cfg.accel_cmps2  = 500.0f;
  Cfg.jerk_cmps3   = 3000.0f;
  Cfg.enable_s_curve = true;  // S-curve habilitada por defecto
  Cfg.crc = 0; Cfg.crc = simpleCRC((uint8_t*)&Cfg, sizeof(Cfg)-sizeof(Cfg.crc));
}

bool loadConfig() {
  EEPROM.get(EEPROM_ADDR, Cfg);
  if (Cfg.magic != CFG_MAGIC) return false;
  uint32_t crc = simpleCRC((uint8_t*)&Cfg, sizeof(Cfg)-sizeof(Cfg.crc));
  return (crc == Cfg.crc);
}

void saveConfig() {
  Cfg.magic = CFG_MAGIC;
  Cfg.crc = 0;
  Cfg.crc = simpleCRC((uint8_t*)&Cfg, sizeof(Cfg)-sizeof(Cfg.crc));
  EEPROM.put(EEPROM_ADDR, Cfg);
  EEPROM.commit();
}

} // namespace App
