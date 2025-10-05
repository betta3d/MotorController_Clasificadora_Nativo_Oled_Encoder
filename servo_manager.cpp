#include "servo_manager.h"
#include <Arduino.h>
#include <driver/mcpwm_prelude.h>
#include "logger.h"

namespace App {

ServoManager ServoMgr;

void ServoManager::init(int gpio){
  gpio_ = gpio;
  pinMode(gpio_, OUTPUT);
  startMcpwm();
  lastUpdateMs_ = millis();
  if (mcpwmOk_) {
    App::logPrintf("SERVO","Init pin=%d backend=MCPWM@50Hz angle=%.1f range=[%u..%u]us",
                   gpio_, angleDeg_, (unsigned)minUs_, (unsigned)maxUs_);
  } else {
    App::logPrintf("SERVO","Init pin=%d backend=DISABLED (MCPWM init failed) range=[%u..%u]us",
                   gpio_, (unsigned)minUs_, (unsigned)maxUs_);
  }
  // Keep output disabled until user toggles Live/Test/Ejecutar to avoid power surges on boot
  setOutputEnabled(false);
}

uint32_t ServoManager::angleToUs(float deg) const {
  if (deg < 0) deg = 0; if (deg > 180) deg = 180;
  float us = (float)minUs_ + (deg/180.0f) * (float)(maxUs_ - minUs_);
  if (us < (float)minUs_) us = (float)minUs_;
  if (us > (float)maxUs_) us = (float)maxUs_;
  return (uint32_t)(us + 0.5f);
}

void ServoManager::applyPulseUs(uint32_t us){
  if (us == lastUsApplied_) return;
  lastUsApplied_ = us;
  if (servoCmp_ && outputEnabled_) {
    // MCPWM runs at 1 MHz, period 20000 ticks for 50 Hz; compare update sets duty in microseconds
    mcpwm_comparator_set_compare_value(servoCmp_, us);
  }
}

void ServoManager::startMcpwm(){
  // Try both MCPWM groups (0 and 1) to maximize compatibility
  mcpwmOk_ = tryStartMcpwmGroup(0) || tryStartMcpwmGroup(1);
}

bool ServoManager::tryStartMcpwmGroup(int group){
  // Create and configure MCPWM timer at 1 MHz, period 20,000 ticks (20 ms)
  mcpwm_timer_config_t tcfg = {};
  tcfg.group_id = group;
  tcfg.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
  tcfg.resolution_hz = 1000000; // 1 MHz -> 1 tick = 1us
  tcfg.count_mode = MCPWM_TIMER_COUNT_MODE_UP;
  tcfg.period_ticks = 20000;
  if (mcpwm_new_timer(&tcfg, &servoTimer_) != ESP_OK) {
    return false;
  }

  mcpwm_operator_config_t ocfg = {};
  ocfg.group_id = group;
  if (mcpwm_new_operator(&ocfg, &servoOper_) != ESP_OK) return false;
  if (mcpwm_operator_connect_timer(servoOper_, servoTimer_) != ESP_OK) return false;

  mcpwm_comparator_config_t cpcfg = {};
  cpcfg.flags.update_cmp_on_tez = 1;
  cpcfg.flags.update_cmp_on_tep = 1;
  if (mcpwm_new_comparator(servoOper_, &cpcfg, &servoCmp_) != ESP_OK) return false;

  mcpwm_generator_config_t gcfg = {};
  gcfg.gen_gpio_num = gpio_;
  if (mcpwm_new_generator(servoOper_, &gcfg, &servoGen_) != ESP_OK) return false;

  if (mcpwm_generator_set_actions_on_timer_event(servoGen_,
      MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)) != ESP_OK) return false;
  if (mcpwm_generator_set_actions_on_compare_event(servoGen_,
      MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, servoCmp_, MCPWM_GEN_ACTION_LOW)) != ESP_OK) return false;

  lastUsApplied_ = 0;
  applyPulseUs(angleToUs(angleDeg_));
  if (mcpwm_timer_enable(servoTimer_) != ESP_OK) return false;
  if (mcpwm_timer_start_stop(servoTimer_, MCPWM_TIMER_START_NO_STOP) != ESP_OK) return false;
  return true;
}

void ServoManager::stopMcpwm(){
  if (servoTimer_) {
    mcpwm_timer_start_stop(servoTimer_, MCPWM_TIMER_STOP_EMPTY);
    mcpwm_timer_disable(servoTimer_);
  }
}

void ServoManager::setTargetAngle(float deg, float vel_mm_s){
  if (deg < 0) deg = 0; if (deg > 180) deg = 180;
  targetDeg_ = deg;
  // Convertir mm/s a deg/s usando mm_per_deg_. Si mm_per_deg_ es 0, fallback
  float deg_per_s = (mm_per_deg_ > 0.0f) ? (vel_mm_s / mm_per_deg_) : vel_mm_s; // si no configurado, interpreta como deg/s
  if (deg_per_s < 1.0f) deg_per_s = 1.0f; // mínimo para que avance
  if (deg_per_s > 360.0f) deg_per_s = 360.0f; // límite razonable
  velDegPerS_ = deg_per_s;
  if (App::LOG_SERVO) {
    App::logPrintf("SERVO","setTargetAngle target=%.1f vel=%.1f %s", targetDeg_, (mm_per_deg_>0)?(vel_mm_s):(velDegPerS_), (mm_per_deg_>0?"mm/s":"deg/s"));
  }
}

void ServoManager::nudgeAngle(float deltaDeg){
  // Maintain velocity units: if mm_per_deg_ > 0 use mm/s, else interpret as deg/s
  float velParam = (mm_per_deg_ > 0.0f) ? (velDegPerS_ * mm_per_deg_) : velDegPerS_;
  setTargetAngle(targetDeg_ + deltaDeg, velParam);
}

void ServoManager::setTargetAngleAbsolute(float deg){
  // Mantener unidad de velocidad actual
  float velParam = (mm_per_deg_ > 0.0f) ? (velDegPerS_ * mm_per_deg_) : velDegPerS_;
  setTargetAngle(deg, velParam);
}

void ServoManager::update(){
  uint32_t now = millis();
  float dt = (lastUpdateMs_>0) ? (now - lastUpdateMs_) / 1000.0f : 0.02f;
  lastUpdateMs_ = now;
  if (dt <= 0) dt = 0.02f;
  // Optional test sweep: auto-scan between 0 and 180 to validate signal regardless of UI
  if (testSweep_) {
    if (testStartMs_ == 0) testStartMs_ = now;
    uint32_t t = now - testStartMs_;
    // 4-second period triangle wave 0..180..0
    float phase = fmodf(t / 4000.0f, 1.0f);
    float deg = (phase < 0.5f) ? (phase * 2.0f * 180.0f) : ((1.0f - (phase - 0.5f) * 2.0f) * 180.0f);
    angleDeg_ = deg;
    targetDeg_ = deg;
    applyPulseUs(angleToUs(angleDeg_));
    return;
  }
  // Avance hacia el objetivo a velocidad velDegPerS_
  float dir = (targetDeg_ > angleDeg_) ? 1.0f : -1.0f;
  float step = velDegPerS_ * dt * dir;
  // Si vamos a pasarnos, clamp al target
  if ((dir > 0 && angleDeg_ + step > targetDeg_) || (dir < 0 && angleDeg_ + step < targetDeg_)){
    angleDeg_ = targetDeg_;
  } else {
    angleDeg_ += step;
  }
  // El pulso siguiente reflejará angleDeg_
  if (mcpwmOk_) applyPulseUs(angleToUs(angleDeg_));
  if (App::LOG_SERVO) {
    uint32_t now = millis();
    bool timeGate = (now - lastLogUpdateMs_) >= 500; // cada 500ms
    bool changeGate = (fabs(angleDeg_ - lastLogAngle_) >= 0.5f) || (fabs(targetDeg_ - angleDeg_) < 0.01f); // cambio notable o llegó al target
    if (timeGate || changeGate) {
      App::logPrintf("SERVO","update angle=%.1f -> %.1f step=%.2f velDeg/s=%.1f live=%d", angleDeg_, targetDeg_, step, velDegPerS_, liveMode_?1:0);
      lastLogUpdateMs_ = now;
    }
  }
}

void ServoManager::setTestSweep(bool on){
  testSweep_ = on;
  setOutputEnabled(on || liveMode_);
  if (!on) testStartMs_ = 0;
}

void ServoManager::setOutputEnabled(bool on){
  outputEnabled_ = on;
  if (!on) {
    // Force LOW when disabled
    pinMode(gpio_, OUTPUT);
    digitalWrite(gpio_, LOW);
  }
}

} // namespace App
