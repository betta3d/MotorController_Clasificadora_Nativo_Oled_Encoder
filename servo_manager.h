#pragma once
#include <stdint.h>
#include <driver/mcpwm_prelude.h>

namespace App {

// Control simple de mini-servo SG90 por PWM 50 Hz usando LEDC
// Ángulo [0..180], velocidad en mm/s (se provee un factor mm_por_grado para convertir a deg/s)

class ServoManager {
public:
  void init(int gpio);
  void setMmPerDeg(float mm_per_deg) { mm_per_deg_ = mm_per_deg; }
  void setTargetAngle(float deg, float vel_mm_s); // mueve con rampa deg/s basada en mm/s
  void setTargetAngleAbsolute(float deg);         // mueve a deg manteniendo la velocidad actual
  void setLive(bool on) { liveMode_ = on; }
  bool live() const { return liveMode_; }
  void setTestSweep(bool on);
  bool testSweep() const { return testSweep_; }
  void setOutputEnabled(bool on);
  bool outputEnabled() const { return outputEnabled_; }
  // Configurar rango de pulsos (us) del servo. Por defecto 500–2400 us.
  void setPulseRange(uint16_t min_us, uint16_t max_us) { if (min_us >= 300 && max_us <= 3000 && min_us < max_us) { minUs_ = min_us; maxUs_ = max_us; } }
  void nudgeAngle(float deltaDeg); // para control por encoder en vivo
  void update(); // llamar en loop
  float angle() const { return angleDeg_; }
  float target() const { return targetDeg_; }
  // Telemetría para UI/diagnóstico
  int gpio() const { return gpio_; }
  bool usingLEDC() const { return false; }
  int ledcChannel() const { return -1; }
  uint16_t minUs() const { return minUs_; }
  uint16_t maxUs() const { return maxUs_; }
  uint32_t currentPulseUs() const { return angleToUs(angleDeg_); }
  uint32_t targetPulseUs()  const { return angleToUs(targetDeg_); }

private:
  int gpio_ = -1;
  // Generación de pulso a 50 Hz mediante MCPWM (1 MHz resolution, 20,000 ticks periodo)
  mcpwm_timer_handle_t servoTimer_ = nullptr;
  mcpwm_oper_handle_t  servoOper_  = nullptr;
  mcpwm_cmpr_handle_t  servoCmp_   = nullptr;
  mcpwm_gen_handle_t   servoGen_   = nullptr;
  uint32_t lastUsApplied_ = 0;
  bool mcpwmOk_ = false;
  bool liveMode_ = false;
  float angleDeg_ = 90.0f;
  float targetDeg_ = 90.0f;
  float velDegPerS_ = 90.0f; // velocidad actual planificada en deg/s
  float mm_per_deg_ = 0.0f;  // conversión: mm por grado (0 => interpretar vel como deg/s)
  uint32_t lastUpdateMs_ = 0;
  uint16_t minUs_ = 544;     // pulso mínimo por defecto (Arduino estándar)
  uint16_t maxUs_ = 2400;    // pulso máximo por defecto
  bool testSweep_ = false;
  uint32_t testStartMs_ = 0;
  bool outputEnabled_ = false;
  // Antirruido de logs
  uint32_t lastLogMs_ = 0;      // última vez que se logueó writeServo
  uint32_t lastLogUpdateMs_ = 0;// última vez que se logueó update()
  uint16_t lastLogUs_ = 0;      // último pulso en us logueado
  float    lastLogAngle_ = -1.0f; // último ángulo logueado

  void startMcpwm();
  bool tryStartMcpwmGroup(int groupId);
  void stopMcpwm();
  void applyPulseUs(uint32_t us);
  uint32_t angleToUs(float deg) const;
};

extern ServoManager ServoMgr;

} // namespace App
