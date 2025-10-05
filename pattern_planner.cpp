#include "pattern_planner.h"
#include "logger.h"
#include <math.h>

namespace App {

PatternPlanner Pattern; // instancia global

void PatternPlanner::init(){
  segCount_ = 0;
  state_ = enabled_ ? PatternState::PP_IDLE : PatternState::PP_DISABLED;
  patternDirty_ = true;
  captureCurrentConfig();
}

void PatternPlanner::setEnabled(bool en){
  enabled_ = en;
  if (!en) {
  state_ = PatternState::PP_DISABLED;
  } else if (state_ == PatternState::PP_DISABLED) {
    state_ = PatternState::PP_IDLE;
    patternDirty_ = true;
  }
}

void PatternPlanner::markDirty(){
  patternDirty_ = true;
}

void PatternPlanner::flush(){
  segCount_ = 0;
  state_ = enabled_ ? PatternState::PP_IDLE : PatternState::PP_DISABLED;
  patternDirty_ = true;
}

void PatternPlanner::captureCurrentConfig(){
  cached_v_slow = Cfg.v_slow_cmps;
  cached_v_med  = Cfg.v_med_cmps;
  cached_v_fast = Cfg.v_fast_cmps;
  cached_accel  = Cfg.accel_cmps2;
  cached_stepsPerRev = stepsPerRev;
  cached_s0 = Cfg.cfg_deg_lento_up;
  cached_s1 = Cfg.cfg_deg_medio;
  cached_s2 = Cfg.cfg_deg_lento_down;
  cached_s3 = Cfg.cfg_deg_travel;
}

bool PatternPlanner::configChanged() const {
  return cached_v_slow != Cfg.v_slow_cmps ||
         cached_v_med  != Cfg.v_med_cmps  ||
         cached_v_fast != Cfg.v_fast_cmps ||
         cached_accel  != Cfg.accel_cmps2 ||
         cached_stepsPerRev != stepsPerRev ||
         memcmp(&cached_s0, &Cfg.cfg_deg_lento_up, sizeof(SectorRange))!=0 ||
         memcmp(&cached_s1, &Cfg.cfg_deg_medio, sizeof(SectorRange))!=0 ||
         memcmp(&cached_s2, &Cfg.cfg_deg_lento_down, sizeof(SectorRange))!=0 ||
         memcmp(&cached_s3, &Cfg.cfg_deg_travel, sizeof(SectorRange))!=0;
}

// Helper local para LONGITUD sector en grados
static float sectorLengthDeg(const SectorRange& s){
  if (s.wraps) return (360.0f - s.start) + s.end; else return (s.end - s.start);
}

// Construye segmentos básicos (uno por sector) y subdivide si la distancia no alcanza accel completa.
void PatternPlanner::rebuildPattern(){
  segCount_ = 0;
  if (!enabled_){ return; }
  if (stepsPerRev == 0 || Cfg.cm_per_rev <= 0){ return; }
  // Pasos por cm
  float steps_per_cm = stepsPerRev / Cfg.cm_per_rev;
  // Velocidades deseadas en pps
  float v_slow_pps = Cfg.v_slow_cmps * steps_per_cm;
  float v_med_pps  = Cfg.v_med_cmps  * steps_per_cm;
  float v_fast_pps = Cfg.v_fast_cmps * steps_per_cm;
  float a_pps2     = (Cfg.accel_cmps2>0? Cfg.accel_cmps2:1.0f) * steps_per_cm;

  struct RawSeg { uint32_t steps; float cruise; } raw[4];
  SectorRange sectors[4] = { Cfg.cfg_deg_lento_up, Cfg.cfg_deg_medio, Cfg.cfg_deg_lento_down, Cfg.cfg_deg_travel };
  float cruises[4] = { v_slow_pps, v_med_pps, v_slow_pps, v_fast_pps };
  uint32_t totalAssigned=0;
  for (int i=0;i<4;i++){
    float deg = sectorLengthDeg(sectors[i]);
    float steps = (deg/360.0f) * stepsPerRev;
    if (steps < 1.0f) steps = 1.0f; // asegurar al menos 1
    raw[i].steps = (uint32_t)floorf(steps);
    raw[i].cruise = cruises[i];
    totalAssigned += raw[i].steps;
  }
  // Ajuste por redondeo: distribuir diferencia
  if (totalAssigned != stepsPerRev){
    int32_t diff = (int32_t)stepsPerRev - (int32_t)totalAssigned;
    int idx=0;
    while (diff!=0){
      raw[idx].steps += (diff>0)?1:-1;
      diff += (diff>0)?-1:1;
      idx = (idx+1)%4;
    }
  }
  // Crear PatternSegs lineales (entry_v, exit_v se rellenarán luego por computeVelProfile)
  uint32_t cursor=0;
  for (int i=0;i<4;i++){
    if (segCount_>=MAX_SEGS) break;
    PatternSeg &ps = segs_[segCount_++];
    ps.start_step = cursor;
    ps.end_step   = cursor + raw[i].steps - 1;
    ps.cruise_v   = raw[i].cruise;
    ps.entry_v = ps.exit_v = 0.0f;
    ps.triangular = false;
    cursor += raw[i].steps;
  }
  // Conectar circularmente las velocidades (lookahead simple)
  computeVelProfile();
  captureCurrentConfig();
  patternDirty_ = false;
  lastRebuildRev_++;
  logPrintf("PNEW","segs=%d stepsPerRev=%lu", segCount_, (unsigned long)stepsPerRev);
}

void PatternPlanner::computeVelProfile(){
  if (segCount_==0) return;
  // Forward continuity: entry del primero mantiene última exit previa (circular)
  // Inicialmente suponemos entry=exit=cruise y luego recortamos por aceleración
  for (uint8_t i=0;i<segCount_;++i){
    segs_[i].entry_v = (i==0)? segs_[segCount_-1].cruise_v : segs_[i-1].cruise_v;
    segs_[i].exit_v  = segs_[i].cruise_v; // provisional
  }
  float a = (Cfg.accel_cmps2>0? Cfg.accel_cmps2:1.0f) * (stepsPerRev / Cfg.cm_per_rev);
  // Iterar: ajustar cruceros para poder desacelerar hacia el siguiente crucero (lookahead circular)
  const int MAX_ITER = 6;
  for (int iter=0; iter<MAX_ITER; ++iter){
    bool changed=false;
    // Backward-style pass (circular): limitar velocidad de cada segmento para alcanzar crucero del siguiente
    for (int i=segCount_-1; i>=0; --i){
      uint8_t idx = (uint8_t)i;
      uint8_t nxt = (idx+1)%segCount_;
      PatternSeg &cur = segs_[idx];
      PatternSeg &next = segs_[nxt];
      uint32_t len = (cur.end_step - cur.start_step + 1);
      float v_start = cur.entry_v;
      float v_end_target = next.cruise_v; // queremos llegar a esto al final
      if (cur.cruise_v > v_end_target){
        // distancia necesaria para frenar desde cruise_v a v_end_target
        float d_dec_needed = (cur.cruise_v*cur.cruise_v - v_end_target*v_end_target)/(2*a);
        if (d_dec_needed > len){
          // No alcanza: reducir cruise_v a máximo alcanzable
          float new_cruise_sq = v_end_target*v_end_target + 2*a*len;
          if (new_cruise_sq < 0) new_cruise_sq=0;
          float new_cruise = sqrtf(new_cruise_sq);
          if (new_cruise < cur.cruise_v - 1e-3f){ cur.cruise_v = new_cruise; changed=true; }
        }
      }
      // Asegurar podemos acelerar desde entry a cruise dentro del segmento
      if (cur.cruise_v > v_start){
        float d_acc_needed = (cur.cruise_v*cur.cruise_v - v_start*v_start)/(2*a);
        if (d_acc_needed > len){
          float new_cruise_sq = v_start*v_start + 2*a*len;
            if (new_cruise_sq<0) new_cruise_sq=0;
          float new_cruise = sqrtf(new_cruise_sq);
          if (new_cruise < cur.cruise_v - 1e-3f){ cur.cruise_v = new_cruise; changed=true; }
        }
      }
      // Marcar triangular si no cabe meseta
      float d_acc = (cur.cruise_v>v_start)? ((cur.cruise_v*cur.cruise_v - v_start*v_start)/(2*a)):0.0f;
      float d_dec = (cur.cruise_v>v_end_target)? ((cur.cruise_v*cur.cruise_v - v_end_target*v_end_target)/(2*a)):0.0f;
      cur.triangular = (d_acc + d_dec) > len;
    }
    // Reencadenar entries (forward continuity)
    for (uint8_t i=0;i<segCount_;++i){
      uint8_t nxt = (i+1)%segCount_;
      segs_[nxt].entry_v = segs_[i].cruise_v;
    }
    if (!changed) break;
  }
  // Finalmente exit_v = entry del siguiente
  for (uint8_t i=0;i<segCount_;++i){
    uint8_t nxt=(i+1)%segCount_;
    segs_[i].exit_v = segs_[nxt].entry_v;
  }
}

PatternSeg* PatternPlanner::findSeg(uint32_t stepMod){
  for (uint8_t i=0;i<segCount_;++i){
    if (stepMod >= segs_[i].start_step && stepMod <= segs_[i].end_step) return &segs_[i];
  }
  return nullptr;
}

void PatternPlanner::service(){
  if (!enabled_) { state_ = PatternState::PP_DISABLED; return; }
  if (state_ == PatternState::PP_DISABLED) state_ = PatternState::PP_IDLE;
  if (configChanged()) patternDirty_ = true;
  uint32_t stepMod = modSteps();
  if (patternDirty_ && stepMod == 0) {
    rebuildPattern();
  state_ = PatternState::PP_READY;
  }
  if (state_ == PatternState::PP_READY && (App::state == SysState::RUNNING || App::state == SysState::ROTATING)) {
    state_ = PatternState::PP_ACTIVE;
  }
  if (!(App::state == SysState::RUNNING || App::state == SysState::ROTATING)) {
  if (state_ == PatternState::PP_ACTIVE) state_ = PatternState::PP_READY; // conservar pattern listo
  }
}

float PatternPlanner::stepVelocity(){
  if (!enabled_ || state_ == PatternState::PP_DISABLED || segCount_==0) return 0.0f;
  uint32_t stepMod = modSteps();
  PatternSeg* s = findSeg(stepMod);
  if (!s) return 0.0f;
  // Distancia dentro del segmento
  uint32_t dist = stepMod - s->start_step;
  uint32_t len  = s->end_step - s->start_step + 1;
  if (len<=1) return s->cruise_v; // mínimo
  float a = (Cfg.accel_cmps2>0? Cfg.accel_cmps2:1.0f) * (stepsPerRev / Cfg.cm_per_rev);
  float v0=s->entry_v, v1=s->exit_v, vc=s->cruise_v;
  // Recalcular distancias aceleración / deceleración
  float d_acc = (vc>v0)? ( (vc*vc - v0*v0)/(2*a) ) : 0.0f;
  float d_dec = (vc>v1)? ( (vc*vc - v1*v1)/(2*a) ) : 0.0f;
  float plateau = (float)len - d_acc - d_dec;
  if (plateau < 0){
    // triangular
    float v_peak_sq = ((2*a*len) + v0*v0 + v1*v1)*0.5f;
    if (v_peak_sq<0) v_peak_sq=0;
    float v_peak = sqrtf(v_peak_sq);
    float d_up = (v_peak*v_peak - v0*v0)/(2*a);
    if (dist <= d_up) {
      return sqrtf(v0*v0 + 2*a*dist);
    } else {
      float d_down = len - d_up;
      float dist_down = dist - d_up;
      float v_sq = v_peak*v_peak - 2*a*dist_down;
      if (v_sq<0) v_sq=0;
      return sqrtf(v_sq);
    }
  } else {
    if (dist < d_acc) {
      return sqrtf(v0*v0 + 2*a*dist);
    } else if (dist < d_acc + plateau) {
      return vc;
    } else {
      float dist_into_dec = dist - (d_acc + plateau);
      float v_sq = vc*vc - 2*a*dist_into_dec;
      if (v_sq<0) v_sq=0;
      float vv = sqrtf(v_sq);
      if (vv < v1) vv = v1;
      return vv;
    }
  }
}

} // namespace App
