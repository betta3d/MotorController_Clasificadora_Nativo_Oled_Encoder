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
  // Cinemática
  Cfg.cm_per_rev   = 20.0f;
  Cfg.v_slow_cmps  = 5.0f;
  Cfg.v_med_cmps   = 12.0f;
  Cfg.v_fast_cmps  = 23.0f;
  Cfg.accel_cmps2  = 60.0f;
  Cfg.jerk_cmps3   = 1500.0f;
  Cfg.enable_s_curve = true;  // S-curve habilitada por defecto

  // Homing
  Cfg.v_home_cmps  = 3.0f;
  Cfg.t_estab_ms   = 2000;
  Cfg.deg_offset   = 0.0f; // -5 grados por defecto
  Cfg.homing_switch_turns  = 1.25f;
  Cfg.homing_timeout_turns = 1.40f;

  // Mecánica
  Cfg.motor_full_steps_per_rev = 200;
  Cfg.microstepping            = 16;
  Cfg.gear_ratio               = 3.0f;

  // Dirección
  Cfg.master_dir_cw = true; // CW por defecto

  // Sectores por defecto (coinciden con globals.cpp)
  Cfg.cfg_deg_lento_up   = {340.0f, 20.0f, true};
  Cfg.cfg_deg_medio      = {20.0f, 160.0f, false};
  Cfg.cfg_deg_lento_down = {160.0f, 200.0f, false};
  Cfg.cfg_deg_travel     = {200.0f, 340.0f, false};

  // WiFi credenciales por defecto vacías
  memset(Cfg.wifi_ssid, 0, sizeof(Cfg.wifi_ssid));
  memset(Cfg.wifi_pass, 0, sizeof(Cfg.wifi_pass));
  Cfg.wifi_valid = 0;

  // Suavizado sectorial por defecto
  Cfg.sector_lookahead_deg = 25.0f; // empezar 20° antes del borde
  Cfg.sector_decel_boost   = 3;  // multiplicar A_MAX al frenar si distancia es corta

  Cfg.crc = 0; Cfg.crc = simpleCRC((uint8_t*)&Cfg, sizeof(Cfg)-sizeof(Cfg.crc));
}

bool loadConfig() {
  EEPROM.get(EEPROM_ADDR, Cfg);
  if (Cfg.magic != CFG_MAGIC) return false;
  uint32_t crc = simpleCRC((uint8_t*)&Cfg, sizeof(Cfg)-sizeof(Cfg.crc));
  if (crc != Cfg.crc) return false;

  // sector_lookahead_deg y sector_decel_boost ya vienen en Cfg; sin lógica adicional

  // Propagar a variables de runtime
  // Cinemática ya está en Cfg (v_slow/v_med/v_fast/accel/jerk)

  // Homing
  V_HOME_CMPS = Cfg.v_home_cmps;
  TIEMPO_ESTABILIZACION_HOME = Cfg.t_estab_ms;
  DEG_OFFSET = Cfg.deg_offset;
  HOMING_SWITCH_TURNS  = (Cfg.homing_switch_turns  > 0.05f) ? Cfg.homing_switch_turns  : 0.70f;
  HOMING_TIMEOUT_TURNS = (Cfg.homing_timeout_turns > 0.10f) ? Cfg.homing_timeout_turns : 1.40f;
  // Saneamiento: timeout >= switch*1.1
  if (HOMING_TIMEOUT_TURNS < HOMING_SWITCH_TURNS * 1.1f) {
    HOMING_TIMEOUT_TURNS = HOMING_SWITCH_TURNS * 1.1f;
  }

  // Mecánica
  MOTOR_FULL_STEPS_PER_REV = Cfg.motor_full_steps_per_rev;
  MICROSTEPPING             = Cfg.microstepping;
  GEAR_RATIO                = Cfg.gear_ratio;
  stepsPerRev = (uint32_t)(MOTOR_FULL_STEPS_PER_REV * MICROSTEPPING * GEAR_RATIO);

  // Dirección
  master_direction = Cfg.master_dir_cw;
  inverse_direction = !master_direction;

  // Sectores
  DEG_LENTO_UP   = Cfg.cfg_deg_lento_up;
  DEG_MEDIO      = Cfg.cfg_deg_medio;
  DEG_LENTO_DOWN = Cfg.cfg_deg_lento_down;
  DEG_TRAVEL     = Cfg.cfg_deg_travel;

  return true;
}

void saveConfig() {
  // Sincronizar desde runtime hacia Cfg antes de calcular CRC
  // Cinemática (ya en Cfg)

  // Homing
  Cfg.v_home_cmps = V_HOME_CMPS;
  Cfg.t_estab_ms  = TIEMPO_ESTABILIZACION_HOME;
  Cfg.deg_offset  = DEG_OFFSET;
  Cfg.homing_switch_turns  = HOMING_SWITCH_TURNS;
  Cfg.homing_timeout_turns = HOMING_TIMEOUT_TURNS;

  // Mecánica
  Cfg.motor_full_steps_per_rev = MOTOR_FULL_STEPS_PER_REV;
  Cfg.microstepping            = MICROSTEPPING;
  Cfg.gear_ratio               = GEAR_RATIO;

  // Dirección
  Cfg.master_dir_cw = master_direction;

  // Sectores
  Cfg.cfg_deg_lento_up   = DEG_LENTO_UP;
  Cfg.cfg_deg_medio      = DEG_MEDIO;
  Cfg.cfg_deg_lento_down = DEG_LENTO_DOWN;
  Cfg.cfg_deg_travel     = DEG_TRAVEL;

  // WiFi (mantener lo que haya en Cfg si ya estaba; nada que sincronizar runtime por ahora)

  Cfg.magic = CFG_MAGIC;
  Cfg.crc = 0;
  Cfg.crc = simpleCRC((uint8_t*)&Cfg, sizeof(Cfg)-sizeof(Cfg.crc));
  EEPROM.put(EEPROM_ADDR, Cfg);
  EEPROM.commit();
}

} // namespace App
