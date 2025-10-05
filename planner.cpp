#include "planner.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "globals.h"
#include "logger.h"

namespace App {

Planner MotionPlanner; // definicion

void Planner::allocateBuffer() {
  if (buffer_) return;
  buffer_ = (Segment*)malloc(sizeof(Segment) * cfg_.segment_buffer_size);
  for (uint8_t i=0;i<cfg_.segment_buffer_size;i++){ buffer_[i].valid=false; }
}

void Planner::init(const PlannerConfig& cfg){
  cfg_ = cfg;
  allocateBuffer();
  head_ = tail_ = count_ = 0;
  active_ = nullptr;
  seg_time_ = 0.0f;
  current_v_ = 0.0f;
}

bool Planner::enqueue(float length_steps, float target_cruise_v){
  if (length_steps <= 0.5f) return true; // ignorar segmentos ínfimos
  if (count_ >= cfg_.segment_buffer_size) return false;
  Segment& s = buffer_[head_];
  s.length_steps = length_steps;
  s.nominal_v = target_cruise_v;
  s.cruise_v = target_cruise_v;
  // Entrada: continuidad con segmento activo o último en cola
  s.entry_v = (count_==0 && !active_) ? v : current_v_;
  if (s.entry_v > s.cruise_v) s.entry_v = s.cruise_v; // clamp
  // exit_v provisional: si hay anterior válido, mantener continuidad, si no 0 (cola vacía)
  if (count_>0) {
    uint8_t prevIdx = (head_ + cfg_.segment_buffer_size - 1) % cfg_.segment_buffer_size;
    if (buffer_[prevIdx].valid) {
      // se ajustará en lookahead; inicializamos como cruise para minimizar frenadas innecesarias
      s.exit_v = buffer_[prevIdx].cruise_v;
    } else {
      s.exit_v = 0.0f;
    }
  } else {
    s.exit_v = 0.0f;
  }
  s.max_entry_v = s.nominal_v; // valor inicial
  s.triangular = false;
  s.valid = true;
  head_ = (head_ + 1) % cfg_.segment_buffer_size;
  count_++;
#if PLANNER_DIAG
  logPrintf("PLNQ","len=%.1f steps cruise=%.1f entry=%.1f cnt=%d", s.length_steps, s.cruise_v, s.entry_v, count_);
#endif
  lookahead();
  return true;
}

void Planner::forwardPass(){
  if (count_==0) return;
  uint8_t idx = tail_;
  float a = cfg_.max_accel <= 0 ? 1.0f : cfg_.max_accel;
  for (uint8_t i=0;i<count_; ++i){
    Segment& s = buffer_[idx];
    if (!s.valid){ idx=(idx+1)%cfg_.segment_buffer_size; continue; }
    // Calcular velocidad alcanzable por aceleración desde start_v en la mitad (para detectar triangular)
    // Distancia necesaria para acelerar de start_v a cruise_v: d = (v^2 - v0^2)/(2a)
  float d_accel = (s.cruise_v > s.entry_v) ? ( (s.cruise_v*s.cruise_v - s.entry_v*s.entry_v)/(2*a) ) : 0.0f;
  float d_decel = (s.cruise_v > s.exit_v) ? ( (s.cruise_v*s.cruise_v - s.exit_v*s.exit_v)/(2*a) ) : 0.0f;
    float total_needed = d_accel + d_decel;
    if (total_needed > s.length_steps) {
      // Triangular: ajustar velocidad pico v_peak usando distancia total
  float v0 = s.entry_v;
  float v1 = s.exit_v; // provisional
      // Supongamos v_peak^2 = ( (2*a*L) + v0^2 + v1^2 ) /2  (derivado de sum dist acel+decel)
      float v_peak_sq = ((2*a*s.length_steps) + v0*v0 + v1*v1)*0.5f;
      if (v_peak_sq < 0) v_peak_sq = 0;
      float v_peak = sqrtf(v_peak_sq);
      s.cruise_v = v_peak;
      s.triangular = true;
    } else {
      s.triangular = false;
    }
    idx = (idx+1)%cfg_.segment_buffer_size;
  }
}

void Planner::backwardPass(){
  if (count_==0) return;
  float a = cfg_.max_accel <= 0 ? 1.0f : cfg_.max_accel;
  // Recorremos hacia atrás ajustando start_v de cada segmento para poder frenar al siguiente
  // Convertimos a indices lineales desde último al primero
  for (int i = count_-2; i >= 0; --i){
    uint8_t idx = (tail_ + i) % cfg_.segment_buffer_size;
    uint8_t nextIdx = (idx + 1) % cfg_.segment_buffer_size;
    Segment& s = buffer_[idx];
    Segment& n = buffer_[nextIdx];
    if (!s.valid || !n.valid) continue;
    // velocidad máxima permitida al final de s para poder llegar a n.start_v con su distancia
  float max_exit = sqrtf( n.entry_v*n.entry_v + 2*a*n.length_steps );
  if (s.cruise_v > max_exit) s.cruise_v = max_exit;
    // Re-evaluar triangular si quedó recortado
  float d_accel = (s.cruise_v > s.entry_v) ? ( (s.cruise_v*s.cruise_v - s.entry_v*s.entry_v)/(2*a) ) : 0;
  float d_decel = (s.cruise_v > s.exit_v) ? ( (s.cruise_v*s.cruise_v - s.exit_v*s.exit_v)/(2*a) ) : 0;
    if (d_accel + d_decel > s.length_steps) s.triangular = true; else s.triangular = false;
  }
}

void Planner::lookahead(){
  if (count_==0) return;
  // Paso 1: establecer continuidad básica: entry_v de i+1 = cruise_v de i
  for (int i=0; i<count_-1; ++i){
    uint8_t idx = (tail_ + i) % cfg_.segment_buffer_size;
    uint8_t nextIdx = (tail_ + i + 1) % cfg_.segment_buffer_size;
    if (buffer_[idx].valid && buffer_[nextIdx].valid){
      buffer_[nextIdx].entry_v = buffer_[idx].cruise_v;
      if (buffer_[nextIdx].entry_v > buffer_[nextIdx].cruise_v)
        buffer_[nextIdx].entry_v = buffer_[nextIdx].cruise_v;
    }
  }
  // Paso 2: Forward: limita cruise por aceleraciones
  forwardPass();
  // Paso 3: Backward: asegura capacidad de frenado encadenado
  backwardPass();
  // Paso 4: Reencadenar exit_v = entry_v siguiente (menos el último)
  for (int i=0; i<count_-1; ++i){
    uint8_t idx = (tail_ + i) % cfg_.segment_buffer_size;
    uint8_t nextIdx = (tail_ + i + 1) % cfg_.segment_buffer_size;
    if (buffer_[idx].valid && buffer_[nextIdx].valid){
      buffer_[idx].exit_v = buffer_[nextIdx].entry_v;
    }
  }
  // Último segmento: sólo frenar a 0 si no esperamos más alimentación (buffer cerca de vacío)
  uint8_t lastIdx = (tail_ + count_-1) % cfg_.segment_buffer_size;
  if (buffer_[lastIdx].valid) {
    if (count_ < (cfg_.segment_buffer_size/2)) {
      buffer_[lastIdx].exit_v = 0.0f; // posible pausa próxima
    } else {
      // Mantener velocidad (no forzar frenado artificial). Salida = cruise.
      buffer_[lastIdx].exit_v = buffer_[lastIdx].cruise_v;
    }
  }
}

float Planner::computeVelocityAt(const Segment& s, float dist_in_seg){
  // Perfil trapezoidal (o triangular) calculado por distancia.
  float a = cfg_.max_accel <= 0 ? 1.0f : cfg_.max_accel;
  // Distancias necesarias para acelerar y frenar
  float d_acc = (s.cruise_v > s.entry_v) ? ( (s.cruise_v*s.cruise_v - s.entry_v*s.entry_v)/(2*a) ) : 0.0f;
  float d_dec = (s.cruise_v > s.exit_v) ? ( (s.cruise_v*s.cruise_v - s.exit_v*s.exit_v)/(2*a) ) : 0.0f;
  float plateau = s.length_steps - d_acc - d_dec;
  if (plateau < 0) {
    // Triangular: recalcular v_peak local real basándonos en dist_in_seg
    // Usamos fórmula general: v^2 = v0^2 + 2 a d (fase subida) hasta un pico.
    // Para simplicidad aproximamos con mitad de la distancia al pico.
    // Aquí haremos un split: encontrar v_peak global y luego calcular local.
  float v0 = s.entry_v, v1 = s.exit_v;
    float v_peak_sq = ((2*a*s.length_steps) + v0*v0 + v1*v1)*0.5f;
    if (v_peak_sq < 0) v_peak_sq = 0;
    float v_peak = sqrtf(v_peak_sq);
    // Fase subida distancia
  float d_up = (v_peak*v_peak - v0*v0)/(2*a);
    if (dist_in_seg <= d_up) {
      return sqrtf(v0*v0 + 2*a*dist_in_seg);
    } else {
      float d_down = s.length_steps - d_up;
      float dist_down = dist_in_seg - d_up;
      // Fase bajada: v^2 = v_peak^2 - 2 a dist_down
      float v_sq = v_peak*v_peak - 2*a*dist_down;
      if (v_sq < 0) v_sq = 0;
      return sqrtf(v_sq);
    }
  } else {
    // Trapezoidal
    if (dist_in_seg < d_acc) {
  return sqrtf(s.entry_v*s.entry_v + 2*a*dist_in_seg);
    } else if (dist_in_seg < d_acc + plateau) {
      return s.cruise_v;
    } else {
      float dist_into_dec = dist_in_seg - (d_acc + plateau);
      float v_sq = s.cruise_v*s.cruise_v - 2*a*dist_into_dec;
      if (v_sq < 0) v_sq = 0;
      float v_ret = sqrtf(v_sq);
      if (v_ret < s.exit_v) v_ret = s.exit_v;
      return v_ret;
    }
  }
}

float Planner::step(float dt){
  // Usamos dt temporalmente para permitir arranque antes de que existan pasos físicos.
  if (!active_) {
    if (count_ == 0) { current_v_ = 0.0f; return current_v_; }
    active_ = &buffer_[tail_];
    active_->start_steps_abs = totalSteps; // anclar inicio
    seg_time_ = 0.0f;
  }
  Segment& s = *active_;
  if (!s.valid) { current_v_ = 0.0f; return current_v_; }
  // Distancia real por pasos ya generados
  int64_t steps_done = totalSteps - s.start_steps_abs;
  if (steps_done < 0) steps_done = 0;
  float dist_in_seg = (float)steps_done;
  // Nuevo: sin bootstrap de distancia ficticia para no desplazar referencia de homing.
  // Mientras no haya pasos físicos, solo avanzamos el tiempo para estimar velocidad inicial suave.
  if (dist_in_seg == 0) {
    seg_time_ += dt;
  } else {
    seg_time_ += dt;
  }
  if (dist_in_seg >= s.length_steps) {
    // Consumir y pasar al siguiente
    s.valid = false;
    tail_ = (tail_ + 1) % cfg_.segment_buffer_size;
    count_--;
    active_ = nullptr;
    return step(dt); // recursivo: activar siguiente o 0
  }
  if (dist_in_seg == 0) {
    // Estimar velocidad solo para feed del control; distancia permanece 0 hasta primer paso real.
    float a = cfg_.max_accel <= 0 ? 1.0f : cfg_.max_accel;
    float v_lin = s.entry_v + a * seg_time_;
    if (v_lin > s.cruise_v) v_lin = s.cruise_v;
    current_v_ = v_lin;
  } else {
    current_v_ = computeVelocityAt(s, dist_in_seg);
  }
#if PLANNER_DIAG
  static uint32_t dbgTick=0;
  // Limitar spam: sólo una vez mientras dist_in_seg==0 y luego cada 250 iteraciones
  static bool loggedZero = false;
  if (dist_in_seg==0 && !loggedZero) {
    logPrintf("PLNS","dist=%.1f/%.1f v=%.1f entry=%.1f cruise=%.1f exit=%.1f tri=%d segs=%d", dist_in_seg, s.length_steps, current_v_, s.entry_v, s.cruise_v, s.exit_v, s.triangular, count_);
    loggedZero = true;
  } else if (dist_in_seg>0 && ((++dbgTick % 250)==0)) {
    logPrintf("PLNS","dist=%.1f/%.1f v=%.1f entry=%.1f cruise=%.1f exit=%.1f tri=%d segs=%d", dist_in_seg, s.length_steps, current_v_, s.entry_v, s.cruise_v, s.exit_v, s.triangular, count_);
  }
  if (dist_in_seg>=s.length_steps-1) loggedZero = false; // permitir para siguiente segmento
#endif
  return current_v_;
}

void Planner::flush(){
  head_ = tail_ = count_ = 0; active_ = nullptr; current_v_ = 0.0f;
}

void Planner::reset(uint8_t newSize){
  // Liberar buffer previo si tamaño cambia
  if (newSize == 0) return;
  if (cfg_.segment_buffer_size != newSize) {
    if (buffer_) { free(buffer_); buffer_ = nullptr; }
    cfg_.segment_buffer_size = newSize;
  }
  allocateBuffer();
  flush();
  // Reinicializar parámetros dinámicos
  active_ = nullptr;
  seg_time_ = 0.0f;
  current_v_ = 0.0f;
}

} // namespace App
