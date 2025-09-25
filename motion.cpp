#include "motion.h"
#include "globals.h"

namespace App {

MotionProfile PROF_SLOW, PROF_MED, PROF_FAST;

static inline void setProfile(MotionProfile& dst, float v_cmps, float a_cmps2, float j_cmps3) {
  float steps_per_cm = (Cfg.cm_per_rev > 0.0f) ? ((float)stepsPerRev / Cfg.cm_per_rev) : 0.0f;
  dst.v_target = v_cmps   * steps_per_cm;
  dst.a_max    = a_cmps2  * steps_per_cm;
  dst.j_max    = j_cmps3  * steps_per_cm;
}

void applyConfigToProfiles() {
  stepsPerRev = (uint32_t)(MOTOR_FULL_STEPS_PER_REV * MICROSTEPPING * GEAR_RATIO);
  setProfile(PROF_SLOW, Cfg.v_slow_cmps, Cfg.accel_cmps2, Cfg.jerk_cmps3);
  setProfile(PROF_MED,  Cfg.v_med_cmps,  Cfg.accel_cmps2, Cfg.jerk_cmps3);
  setProfile(PROF_FAST, Cfg.v_fast_cmps, Cfg.accel_cmps2, Cfg.jerk_cmps3);
  
  // DEBUG: Mostrar perfiles calculados
  Serial.printf("[DEBUG] Perfiles aplicados:\n");
  Serial.printf("  SLOW: v=%.1f pps, a=%.1f pps², j=%.1f pps³\n", PROF_SLOW.v_target, PROF_SLOW.a_max, PROF_SLOW.j_max);
  Serial.printf("  MED:  v=%.1f pps, a=%.1f pps², j=%.1f pps³\n", PROF_MED.v_target, PROF_MED.a_max, PROF_MED.j_max);
  Serial.printf("  FAST: v=%.1f pps, a=%.1f pps², j=%.1f pps³\n", PROF_FAST.v_target, PROF_FAST.a_max, PROF_FAST.j_max);
}

void selectSectorProfile(float deg) {
  if (inSectorRange(deg, DEG_LENTO)) {
    v_goal = PROF_SLOW.v_target; A_MAX = PROF_SLOW.a_max; J_MAX = PROF_SLOW.j_max;
  } else if (inSectorRange(deg, DEG_MEDIO)) {
    v_goal = PROF_MED.v_target;  A_MAX = PROF_MED.a_max;  J_MAX = PROF_MED.j_max;
  } else if (inSectorRange(deg, DEG_RAPIDO)) {
    v_goal = PROF_FAST.v_target; A_MAX = PROF_FAST.a_max; J_MAX = PROF_FAST.j_max;
  } else {
    v_goal = PROF_SLOW.v_target; A_MAX = PROF_SLOW.a_max; J_MAX = PROF_SLOW.j_max;
  }
}

} // namespace App
