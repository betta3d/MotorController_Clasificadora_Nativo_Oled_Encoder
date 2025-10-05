#pragma once
#include <stdint.h>
#include <stddef.h>

// Debug macro (enable to get planner queue and segment logs)
#ifndef PLANNER_DIAG
#define PLANNER_DIAG 1
#endif

// Planner estilo Marlin simplificado para 1 eje rotacional continuo con sectores.
// Objetivo: reemplazar gradualmente la logica basada en perfiles sectoriales inmediatos
// por un buffer de segmentos con lookahead y limitacion jerk/aceleracion global.
// Fase 1: Generar segmentos ciclicos que representan cada sector con su velocidad de crucero.
// Fase 2: Implementar lookahead multi-segmento (n=3..8) recalculando entry/exit speed.
// Fase 3: Integrar generador de pasos (ya existente) leyendo v(t) planificada.

namespace App {

struct PlannerConfig {
  float max_accel;       // pps^2
  float max_jerk;        // pps^3 (jerk total uniforme)
  float junction_deviation; // similar a Marlin (en pasos) para blending (opcional)
  uint8_t segment_buffer_size; // tamaño ring buffer
};

// Segmento basado en distancia (estilo Marlin simplificado)
struct Segment {
  float length_steps    = 0.0f;  // distancia total del segmento (steps)
  float entry_v         = 0.0f;  // velocidad efectiva de entrada
  float exit_v          = 0.0f;  // velocidad efectiva de salida
  float nominal_v       = 0.0f;  // velocidad solicitada (perfil sector)
  float cruise_v        = 0.0f;  // velocidad alcanzable tras limitaciones
  float max_entry_v     = 0.0f;  // velocidad máxima admisible al entrar (forward pass)
  bool  valid        = false;
  bool  triangular   = false;    // true si no hay meseta de crucero
  int64_t start_steps_abs = 0;   // totalSteps cuando se activa
};

class Planner {
public:
  void init(const PlannerConfig& cfg);
  bool enqueue(float length_steps, float target_cruise_v);
  void lookahead();        // recalcular entradas/salidas (forward + backward simplificado)
  float step(float dt);    // devuelve v planificada segun distancia recorrida del segmento
  float currentPlannedVelocity() const { return current_v_; }
  bool empty() const { return count_ == 0; }
  uint8_t size() const { return count_; }
  void flush();
  void reset(uint8_t newSize);

private:
  PlannerConfig cfg_{};
  Segment* buffer_ = nullptr;
  uint8_t head_ = 0;
  uint8_t tail_ = 0;
  uint8_t count_ = 0;
  Segment* active_ = nullptr;
  float current_v_ = 0.0f;
  float seg_time_ = 0.0f; // mantenido para compatibilidad (dt disponible)

  void allocateBuffer();
  void forwardPass();
  void backwardPass();
  void adjustSegment(Segment& s);
  float computeVelocityAt(const Segment& s, float dist_in_seg);
};

extern Planner MotionPlanner; // instancia global

} // namespace App
