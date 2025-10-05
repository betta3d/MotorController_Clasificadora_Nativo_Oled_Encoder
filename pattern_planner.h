#pragma once
#include <stdint.h>
#include <stddef.h>
#include "globals.h"

// PatternPlanner: precomputa una vuelta completa en pasos con segmentos de velocidad.
// Aplica perfiles trapezoidales/triangulares internamente por segmento y reutiliza el patrón cíclicamente.
// Reconstrucción diferida: patternDirty se marca al cambiar parámetros y se reconstruye al cruzar step 0.

namespace App {

struct PatternSeg {
  uint32_t start_step;   // offset dentro de la vuelta (0 .. stepsPerRev-1)
  uint32_t end_step;     // inclusive end (end_step < stepsPerRev)
  float    entry_v;      // pps planificada al inicio del segmento (después de lookahead interno)
  float    exit_v;       // pps al final
  float    cruise_v;     // pps objetivo
  bool     triangular;   // true si no hay meseta
};

// Nota: evitar nombre DISABLED porque Arduino/ESP32 define macro DISABLED en headers GPIO.
enum class PatternState : uint8_t { PP_DISABLED, PP_IDLE, PP_READY, PP_ACTIVE };

class PatternPlanner {
public:
  void init();
  void setEnabled(bool en);
  bool enabled() const { return enabled_; }

  // Marcar dirty cuando cambian sectores/velocidades/aceleración
  void markDirty();

  // Llamar periódicamente (en controlTick) antes de usar stepVelocity
  void service();

  // Devuelve velocidad planificada para el step actual (usa totalSteps mod stepsPerRev)
  float stepVelocity();

  // Forzar flush total
  void flush();

  // Instrumentación básica
  bool isDirty() const { return patternDirty_; }
  PatternState state() const { return state_; }
  uint8_t segmentCount() const { return segCount_; }

private:
  static const uint8_t MAX_SEGS = 12; // suficiente para 4 sectores + subdivisiones
  PatternSeg segs_[MAX_SEGS];
  uint8_t segCount_ = 0;
  bool patternDirty_ = true;
  bool enabled_ = false;
  PatternState state_ = PatternState::PP_DISABLED;
  uint32_t lastRebuildRev_ = 0; // contador de vueltas en las que se reconstruyó

  // Cache parámetros usados para detectar cambios implícitos
  float cached_v_slow = 0, cached_v_med = 0, cached_v_fast = 0;
  float cached_accel = 0;
  uint32_t cached_stepsPerRev = 0;
  SectorRange cached_s0{}, cached_s1{}, cached_s2{}, cached_s3{};

  void captureCurrentConfig();
  bool configChanged() const;
  void rebuildPattern();
  void computeVelProfile();
  PatternSeg* findSeg(uint32_t stepMod);
};

extern PatternPlanner Pattern;

} // namespace App
