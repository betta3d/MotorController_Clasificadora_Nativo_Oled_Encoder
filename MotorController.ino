#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include "pins.h"
#include "state.h"
#include "globals.h"
#include "eeprom_store.h"
#include "motion.h"
#include "encoder.h"
#include "io.h"
#include "oled_ui.h"
#include "control.h"
#include "commands.h"
#include "commands_control.h"
#include "buzzer.h"
#include "logger.h"
#include "homing.h"
#include "wifi_manager.h"
#include "planner.h"
#include "servo_manager.h"

using namespace App;

void setup() {
  Serial.begin(115200);
  delay(50);

  EEPROM.begin(512);
  if (!loadConfig()) { setDefaults(); saveConfig(); }
  // stepsPerRev y perfiles ya quedan consistentes por loadConfig(); recalculamos por seguridad
  stepsPerRev = (uint32_t)(MOTOR_FULL_STEPS_PER_REV * MICROSTEPPING * GEAR_RATIO);
  applyConfigToProfiles();

  ioInit();           // pines, I2C y LEDs
  encoderInit();      // encoder por ISR
  oledInit();         // OLED
  Buzzer::init();
  initNewMenuModel(); // Nuevo menú jerárquico
  controlStart();     // esp_timer 1 kHz
  WifiMgr::init();    // WiFi manager (fase 1A)
  // Servo disabled (reverted): do not initialize to avoid any side-effects
  // ServoMgr.init(PIN_SERVO);

  digitalWrite(PIN_STEP, LOW);
  setDirection(true); // Inicializar usando dirección maestra configurada
  totalSteps = 0; v = 0.0f; a = 0.0f; v_goal = 0.0f;
  state = SysState::UNHOMED; homed = false;
  uiScreen = UiScreen::STATUS;

  // Inicializar sistema de logging
  initLogging();
  // SERVO: logs deshabilitados por defecto (activarlos solo para diagnosticar)
  App::setLogEnabled("SERVO", false);

  // === Planner (modo sombra inicial) ===
  {
    App::PlannerConfig pcfg;
    pcfg.max_accel = Cfg.accel_cmps2 * ((Cfg.cm_per_rev>0)? (stepsPerRev / Cfg.cm_per_rev):0); // convertir a pps^2
    pcfg.max_jerk = Cfg.jerk_cmps3 * ((Cfg.cm_per_rev>0)? (stepsPerRev / Cfg.cm_per_rev):0);  // pps^3 (placeholder)
    pcfg.junction_deviation = 0.0f; // aún sin uso
    pcfg.segment_buffer_size = 8;
      pcfg.max_accel = App::Cfg.accel_cmps2 * ( (App::stepsPerRev>0 && App::Cfg.cm_per_rev>0)? ( (float)App::stepsPerRev / App::Cfg.cm_per_rev ) : 0 );
      if (pcfg.max_accel <= 0) pcfg.max_accel = 1000.0f; // valor seguro
      pcfg.max_jerk = App::Cfg.jerk_cmps3; // conversión a pasos^3/s^3 se puede añadir si se usa
      pcfg.junction_deviation = 0.0f; // aún no usado
      pcfg.segment_buffer_size = App::Cfg.planner_buffer_size;
      MotionPlanner.init(pcfg);

    // Crear 4 segmentos base (una vuelta) según sectores actuales.
    auto addSector=[&](const SectorRange& s, float cruise_v_cmps){
      // Longitud angular del sector (considerando wrap)
      float ang;
      if (s.wraps) {
        if (s.start <= s.end) ang = 360.0f; else ang = (360.0f - s.start) + s.end; // ex: 350-10 -> 20°
      } else {
        ang = (s.end - s.start);
      }
      if (ang < 0) ang += 360.0f;
      float revs = ang / 360.0f;
      float steps = revs * stepsPerRev;
      float steps_per_cm = (Cfg.cm_per_rev>0)? (stepsPerRev / Cfg.cm_per_rev):0;
      float cruise_pps = cruise_v_cmps * steps_per_cm;
      MotionPlanner.enqueue(steps, cruise_pps);
    };
    addSector(DEG_LENTO_UP,   Cfg.v_slow_cmps);
    addSector(DEG_MEDIO,      Cfg.v_med_cmps);
    addSector(DEG_LENTO_DOWN, Cfg.v_slow_cmps);
    addSector(DEG_TRAVEL,     Cfg.v_fast_cmps);
    logPrint("PLANNER", "Planner shadow inicializado con 4 segmentos (1 vuelta)");
  }
  
  logPrint("SYSTEM", "ESP32 + TB6600 — Proyecto modular listo.");
  logPrint("SYSTEM", "Seguridad: no inicia RUNNING hasta completar HOME.");
  logPrint("SYSTEM", "Comandos seriales principales:");
  logPrint("SYSTEM", "  STATUS         - Muestra configuracion completa con ejemplos");
  logPrint("SYSTEM", "  SCURVE=ON/OFF  - Habilita/deshabilita curvas S");
  logPrint("SYSTEM", "  ROTAR=N        - Rota N vueltas (+ CW, - CCW)");
  logPrint("SYSTEM", "  STOP           - Detiene movimiento");
  logPrint("SYSTEM", "Configuracion (ejemplos):");
  logPrint("SYSTEM", "  V_SLOW=5.0 | V_MED=10.0 | V_FAST=15.0 | ACCEL=50.0");
  logPrint("SYSTEM", "  DEG_LENTO_UP=350-10 | DEG_MEDIO=10-170 | DEG_LENTO_DOWN=170-190 | DEG_TRAVEL=190-350");
  logPrint("SYSTEM", "  MICROSTEPPING=16 | GEAR_RATIO=1.0");
  logPrint("SYSTEM", "Use STATUS para ver TODOS los parametros y comandos disponibles.");
  logPrint("SYSTEM", "Use LOG-STATUS para ver control de logging.");
}

void loop() {
  uint32_t now = millis();
  // Servo disabled (reverted): no update
  // ServoMgr.update();

  // Botón HOME físico
  if (btnHomePhys() && (now - lastBtnHomeMs > DEBOUNCE_MS)) {
    lastBtnHomeMs = now;
    if (state != SysState::RUNNING) {
      App::startCentralizedHoming();
      logPrint("HOME", "Iniciando homing centralizado (fisico)...");
    }
  }

  // Botón START/STOP físico
  if (btnStartPhys() && (now - lastBtnStartMs > DEBOUNCE_MS)) {
    lastBtnStartMs = now;
    if (state == SysState::RUNNING) {
      state = SysState::STOPPING;
      logPrint("START_STOP", "Deteniendo con rampa...");
    } else {
      if (!homed) {
        logPrint("START_STOP", "BLOQUEADO: haga HOME primero.");
      } else if (state == SysState::READY) {
        v_goal = 0.0f; a = 0.0f;
        revStartModSteps = modSteps();
        ledVerdeBlinkState = false; ledBlinkLastMs = now;
        state = SysState::RUNNING;
        logPrint("START_STOP", "RUNNING: sectores activos.");
  uiScreen = UiScreen::STATUS; // screensaver eliminado
      }
    }
  }

  // FSM alto nivel
  switch (state) {
    case SysState::RUNNING:
      if (RUN_MODE == RunMode::ONE_REV && v_goal <= 0.0f) {
        state = SysState::STOPPING; logPrint("RUN", "1 vuelta completa. Parando...");
      }
      break;

    case SysState::ROTATING:
      // La lógica de completado está en control.cpp
      break;

    case SysState::STOPPING:
      if (v >= 1.0f) v_goal = 0.0f;
      else { state = SysState::READY; logPrint("RUN", "Motor detenido. READY."); }
      break;

    default: break;
  }

  // ===== PROCESAMIENTO DE HOMING CENTRALIZADO =====
  if (state == SysState::HOMING_SEEK) {
    App::processCentralizedHoming();
    
    // Si homing se completó exitosamente, ejecutar rotación pendiente si la hay
    if (App::centralizedHomingCompleted()) {
      if (homed && App::homingCtx.phase == App::HomingPhase::DONE) {
        // Homing exitoso - verificar si hay rotación pendiente
        if (Comandos::ejecutarRotacionPendiente()) {
          Serial.println("[MAIN] Iniciando rotación pendiente tras homing exitoso");
        } else {
          Serial.println("[MAIN] Homing completado, sin rotación pendiente");
        }
      } else if (App::homingCtx.phase == App::HomingPhase::FAULT) {
        Serial.println("[MAIN] Homing falló, cancelando rotación pendiente");
        pendingRotateRevs = 0.0f; // Cancelar rotación pendiente
      }
    }
  }

  // ===== UI con encoder (nuevo flujo centralizado en oled_ui.cpp) =====
  int8_t d = encoderReadDelta();
  bool click = encoderReadClick();

  // ===== DEBUG ENCODER =====
  static uint32_t lastDebugPrintMs = 0;
  if (millis() - lastDebugPrintMs > 100) { // Imprime cada 100ms si hay evento
    lastDebugPrintMs = millis();
    if (d != 0 || click) {
      logPrintf("DEBUG", "ENCODER EVENT | Delta: %d, Click: %s", d, click ? "true" : "false");
    }
  }
  // ===== FIN DEBUG =====

  uiProcess(d, click);   // procesa navegación/edición
  uiRender();            // dibuja la pantalla actual
  Buzzer::update();      // gestiona beepNav no bloqueante
  WifiMgr::tick();       // estado WiFi

  // ===== COMANDOS SERIALES (delegado a commands.cpp) =====
  processCommands();

  // Telemetría solo en cambio de estado
  static SysState lastState = SysState::UNHOMED;
  if (state != lastState) {
    lastState = state;
    float ang = currentAngleDeg();
    float steps_per_cm = (Cfg.cm_per_rev > 0.0f) ? ((float)stepsPerRev / Cfg.cm_per_rev) : 0.0f;
    float v_cmps = (steps_per_cm > 0.0f) ? (v / steps_per_cm) : 0.0f;
    logPrintf("TELEMETRIA", "STATE=%s | HOMED=%d | OPT_ACTIVE=%d | S-CURVE=%s | v=%.1f | a=%.1f | v_goal=%.1f | A_MAX=%.1f | J_MAX=%.1f | v_cmps=%.1f",
              stateName(state), homed ? 1 : 0, optActive() ? 1 : 0, Cfg.enable_s_curve ? "ON" : "OFF", v, a, v_goal, A_MAX, J_MAX, v_cmps);
  }

  // === Planner shadow sampling ===
  // Cada 100 ms muestreamos la velocidad planificada para comparación.
  static uint32_t lastPlannerMs = 0;
  if (millis() - lastPlannerMs >= 100) {
    lastPlannerMs = millis();
    // Avanzar planner con dt acumulado aproximado (0.1s) sin afectar control.
    float v_plan = MotionPlanner.step(0.1f);
    // Mostrar comparación si hay movimiento o plan no vacío
    if (!MotionPlanner.empty() || v_plan>0.1f) {
      logPrintf("PLANNER_SHADOW", "v_plan=%.1f pps | v_goal=%.1f pps | v=%.1f", v_plan, v_goal, v);
    }
  }
}
