#include "motion.h"
#include "globals.h"
#include "logger.h"

namespace App {

MotionProfile PROF_SLOW, PROF_MED, PROF_FAST;

static inline void setProfile(MotionProfile& dst, float v_cmps, float a_cmps2, float j_cmps3) {
  float steps_per_cm = (Cfg.cm_per_rev > 0.0f) ? ((float)stepsPerRev / Cfg.cm_per_rev) : 0.0f;
  dst.v_target = v_cmps   * steps_per_cm;
  dst.a_max    = a_cmps2  * steps_per_cm;
  dst.j_max    = j_cmps3  * steps_per_cm;
}

// Devuelve distancia angular (deg) hasta el inicio de un sector objetivo, asumiendo sentido positivo.
static float degDistanceForward(float fromDeg, const SectorRange& target){
  float d = 0.0f;
  // Normalizar
  auto norm=[&](float x){ while(x<0)x+=360.0f; while(x>=360.0f)x-=360.0f; return x; };
  fromDeg = norm(fromDeg);
  // Si target.wraps: intervalo [start..360) U [0..end]
  // Buscamos la mínima distancia positiva desde fromDeg al primer punto del sector (start)
  float start = norm(target.start);
  if (target.wraps){
    if (fromDeg <= target.end || fromDeg >= start){ return 0.0f; } // ya dentro
    // distancia hasta start
    if (fromDeg < start) d = start - fromDeg; else d = (360.0f - fromDeg) + start;
  } else {
    // Sector no envuelve
    bool inside = (fromDeg >= target.start && fromDeg <= target.end);
    if (inside) return 0.0f;
    if (fromDeg < target.start) d = target.start - fromDeg; else d = (360.0f - fromDeg) + target.start;
  }
  return d;
}

// Determina la velocidad objetivo del siguiente sector en el sentido de avance (solo para blending simple).
static float sectorBaseVTarget(float deg){
  if (inSectorRange(deg, DEG_LENTO_UP))   return PROF_SLOW.v_target;
  if (inSectorRange(deg, DEG_MEDIO))      return PROF_MED.v_target;
  if (inSectorRange(deg, DEG_LENTO_DOWN)) return PROF_SLOW.v_target;
  if (inSectorRange(deg, DEG_TRAVEL))     return PROF_FAST.v_target;
  return PROF_SLOW.v_target;
}

// dado un ángulo actual y velocidades de sector actual y próximo, devuelve v blend.
static float blendedV(float deg){
  float look = Cfg.sector_lookahead_deg;
  if (look <= 0.1f) return sectorBaseVTarget(deg); // sin blending

  // Identificar sector actual y el siguiente (orden fijo: LENTO_UP -> MEDIO -> LENTO_DOWN -> TRAVEL -> LENTO_UP)
  const SectorRange* order[4] = { &DEG_LENTO_UP, &DEG_MEDIO, &DEG_LENTO_DOWN, &DEG_TRAVEL };
  int currentIdx = -1;
  for (int i=0;i<4;i++){ if (inSectorRange(deg, *order[i])) { currentIdx = i; break; } }
  if (currentIdx < 0) return sectorBaseVTarget(deg);
  int nextIdx = (currentIdx + 1) % 4;
  float vNow = sectorBaseVTarget(deg);
  // Distancia angular hasta inicio del siguiente sector
  float dist = degDistanceForward(deg, *order[nextIdx]);
  if (dist <= 0.0f){
    return vNow; // ya estamos en su borde (o dentro) - la selección base se encargará
  }
  float vNext = sectorBaseVTarget(order[nextIdx]->start);
  if (vNext == vNow) return vNow; // no hay cambio

  if (dist > look){
    return vNow; // fuera de ventana de blending
  }
  // factor inverso proporcional a cuán cerca estamos del borde
  float f = (look - dist)/look; if (f < 0) f=0; if (f>1) f=1;
  // Si vamos a desacelerar (vNext < vNow) podemos empezar a bajar antes usando boost para planear
  if (vNext < vNow){
    // f puede acelerarse al cuadrado para adelantar más la caída
    f = f*f; // adelanta un poco la reducción
  }
  float vOut = vNow + f * (vNext - vNow);
#ifdef ENABLE_BLEND_DEBUG
  App::logPrintf("BLEND","deg=%.1f dist=%.2f look=%.1f f=%.2f vNow=%.1f vNext=%.1f vOut=%.1f", deg, dist, look, f, vNow, vNext, vOut);
#endif
  return vOut;
}

void applyConfigToProfiles() {
  stepsPerRev = (uint32_t)(MOTOR_FULL_STEPS_PER_REV * MICROSTEPPING * GEAR_RATIO);
  setProfile(PROF_SLOW, Cfg.v_slow_cmps, Cfg.accel_cmps2, Cfg.jerk_cmps3);
  setProfile(PROF_MED,  Cfg.v_med_cmps,  Cfg.accel_cmps2, Cfg.jerk_cmps3);
  setProfile(PROF_FAST, Cfg.v_fast_cmps, Cfg.accel_cmps2, Cfg.jerk_cmps3);
  
  // DEBUG: Mostrar perfiles calculados
  logPrint("DEBUG", "Perfiles aplicados:");
  logPrintf("DEBUG", "  SLOW: v=%.1f pps, a=%.1f pps², j=%.1f pps³", PROF_SLOW.v_target, PROF_SLOW.a_max, PROF_SLOW.j_max);
  logPrintf("DEBUG", "  MED:  v=%.1f pps, a=%.1f pps², j=%.1f pps³", PROF_MED.v_target, PROF_MED.a_max, PROF_MED.j_max);
  logPrintf("DEBUG", "  FAST: v=%.1f pps, a=%.1f pps², j=%.1f pps³", PROF_FAST.v_target, PROF_FAST.a_max, PROF_FAST.j_max);
}

void selectSectorProfile(float deg) {
  // Base según sector (para A_MAX/J_MAX del perfil actual)
  if (inSectorRange(deg, DEG_LENTO_UP))      { A_MAX = PROF_SLOW.a_max; J_MAX = PROF_SLOW.j_max; }
  else if (inSectorRange(deg, DEG_MEDIO))    { A_MAX = PROF_MED.a_max;  J_MAX = PROF_MED.j_max; }
  else if (inSectorRange(deg, DEG_LENTO_DOWN)){ A_MAX = PROF_SLOW.a_max; J_MAX = PROF_SLOW.j_max; }
  else if (inSectorRange(deg, DEG_TRAVEL))   { A_MAX = PROF_FAST.a_max; J_MAX = PROF_FAST.j_max; }
  else { A_MAX = PROF_SLOW.a_max; J_MAX = PROF_SLOW.j_max; }

  // Blended velocity target
  float vBlend = blendedV(deg);
  v_goal = vBlend;

  // Ajuste de frenado si estamos mucho más rápido y la transición es a velocidad menor
  if (v > v_goal && Cfg.sector_decel_boost > 1.0f){
    // Aplica un boost temporal a A_MAX para ayudar a alcanzar la meta dentro del sector
    A_MAX *= Cfg.sector_decel_boost;
  }
}

} // namespace App
