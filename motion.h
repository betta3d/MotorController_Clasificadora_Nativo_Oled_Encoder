#pragma once
namespace App {

struct MotionProfile { float v_target, a_max, j_max; }; // pps, pps^2, pps^3
extern MotionProfile PROF_SLOW, PROF_MED, PROF_FAST;

void applyConfigToProfiles();          // convierte cm/s a pps según stepsPerRev
void selectSectorProfile(float deg);   // elige slow/med/fast según ángulo

} // namespace App
