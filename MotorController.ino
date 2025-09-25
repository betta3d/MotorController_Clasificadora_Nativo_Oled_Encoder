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

using namespace App;

void setup() {
  Serial.begin(115200);
  delay(50);

  EEPROM.begin(512);
  if (!loadConfig()) { setDefaults(); saveConfig(); }
  stepsPerRev = (uint32_t)(MOTOR_FULL_STEPS_PER_REV * MICROSTEPPING * GEAR_RATIO);
  applyConfigToProfiles();
  HOMING_TIMEOUT_STEPS = 5 * stepsPerRev;

  ioInit();           // pines, I2C y LEDs
  encoderInit();      // encoder por ISR
  oledInit();         // OLED
  controlStart();     // esp_timer 1 kHz

  digitalWrite(PIN_STEP, LOW);
  setDirection(true);
  totalSteps = 0; v = 0.0f; a = 0.0f; v_goal = 0.0f;
  state = SysState::UNHOMED; homed = false;
  uiScreen = UiScreen::STATUS;

  Serial.println("\nESP32 + TB6600 — Proyecto modular listo.");
  Serial.println("Seguridad: no inicia RUNNING hasta completar HOME.");
  Serial.println("\nComandos seriales principales:");
  Serial.println("  STATUS         - Muestra configuracion completa con ejemplos");
  Serial.println("  SCURVE=ON/OFF  - Habilita/deshabilita curvas S");
  Serial.println("  ROTAR=N        - Rota N vueltas (+ CW, - CCW)");
  Serial.println("  STOP           - Detiene movimiento");
  Serial.println("\nConfiguracion (ejemplos):");
  Serial.println("  V_SLOW=5.0 | V_MED=10.0 | V_FAST=15.0 | ACCEL=50.0");
  Serial.println("  DEG_LENTO=355-10 | DEG_MEDIO=10-180 | DEG_RAPIDO=180-355");
  Serial.println("  MICROSTEPPING=16 | GEAR_RATIO=1.0");
  Serial.println("\nUse STATUS para ver TODOS los parametros y comandos disponibles.");
}

void loop() {
  uint32_t now = millis();

  // Botón HOME físico
  if (btnHomePhys() && (now - lastBtnHomeMs > DEBOUNCE_MS)) {
    lastBtnHomeMs = now;
    if (state != SysState::RUNNING) {
      startHoming();
      Serial.println("[HOME] Iniciando homing (fisico)...");
    }
  }

  // Botón START/STOP físico
  if (btnStartPhys() && (now - lastBtnStartMs > DEBOUNCE_MS)) {
    lastBtnStartMs = now;
    if (state == SysState::RUNNING) {
      state = SysState::STOPPING;
      Serial.println("[START/STOP] Deteniendo con rampa...");
    } else {
      if (!homed) {
        Serial.println("[START/STOP] BLOQUEADO: haga HOME primero.");
      } else if (state == SysState::READY) {
        v_goal = 0.0f; a = 0.0f;
        revStartModSteps = modSteps();
        ledVerdeBlinkState = false; ledBlinkLastMs = now;
        state = SysState::RUNNING;
        Serial.println("[START/STOP] RUNNING: sectores activos.");
        uiScreen = UiScreen::STATUS;
        screensaverActive = false; // Ensure screensaver is off initially
        screensaverStartTime = now; // Start screensaver timer
      }
    }
  }

  // FSM alto nivel
  switch (state) {
    case SysState::HOMING_SEEK:
      if (optActive()) { homingStepCounter = 0; state = SysState::HOMING_BACKOFF; Serial.println("[HOME] Sensor detectado. Backoff..."); }
      else if (homingStepCounter > HOMING_TIMEOUT_STEPS) { state = SysState::FAULT; Serial.println("[HOME] TIMEOUT SEEK."); }
      break;

    case SysState::HOMING_BACKOFF: {
      uint32_t backoffSteps = (uint32_t)(HOMING_BACKOFF_DEG / degPerStep());
      if (homingStepCounter >= backoffSteps) {
        homingStepCounter = 0; state = SysState::HOMING_REAPP; Serial.println("[HOME] Backoff OK. Re-approach lento...");
      }
    } break;

    case SysState::HOMING_REAPP:
      if (optActive()) { v_goal = 0.0f; delay(50); setZeroHere(); state = SysState::READY; Serial.println("[HOME] CERO fijado. READY."); }
      else if (homingStepCounter > HOMING_TIMEOUT_STEPS) {
        state = SysState::FAULT;
        v = 0.0f; // Reset velocity immediately
        a = 0.0f; // Reset acceleration immediately
        Serial.println("[HOME] TIMEOUT REAPP.");
      }
      break;

    case SysState::RUNNING:
      if (RUN_MODE == RunMode::ONE_REV && v_goal <= 0.0f) {
        state = SysState::STOPPING; Serial.println("[RUN] 1 vuelta completa. Parando...");
      }
      break;

    case SysState::ROTATING:
      // La lógica de completado está en control.cpp
      break;

    case SysState::STOPPING:
      if (v >= 1.0f) v_goal = 0.0f;
      else { state = SysState::READY; Serial.println("[STOP] Motor detenido. READY."); }
      break;

    default: break;
  }

  // ===== UI con encoder (nuevo flujo centralizado en oled_ui.cpp) =====
  int8_t d = encoderReadDelta();
  bool click = encoderReadClick();

  // ===== DEBUG ENCODER =====
  static uint32_t lastDebugPrintMs = 0;
  if (millis() - lastDebugPrintMs > 100) { // Imprime cada 100ms si hay evento
    lastDebugPrintMs = millis();
    if (d != 0 || click) {
      Serial.printf("--> ENCODER EVENT | Delta: %d, Click: %s\n", d, click ? "true" : "false");
    }
  }
  // ===== FIN DEBUG =====

  uiProcess(d, click);   // procesa navegación/edición
  uiRender();            // dibuja la pantalla actual

  // ===== COMANDOS SERIALES (delegado a commands.cpp) =====
  processCommands();

  // Telemetría solo en cambio de estado
  static SysState lastState = SysState::UNHOMED;
  if (state != lastState) {
    lastState = state;
    float ang = currentAngleDeg();
    float steps_per_cm = (Cfg.cm_per_rev > 0.0f) ? ((float)stepsPerRev / Cfg.cm_per_rev) : 0.0f;
    float v_cmps = (steps_per_cm > 0.0f) ? (v / steps_per_cm) : 0.0f;
    Serial.printf("STATE=%s | HOMED=%d | OPT_ACTIVE=%d | S-CURVE=%s | v=%.1f | a=%.1f | v_goal=%.1f | A_MAX=%.1f | J_MAX=%.1f | v_cmps=%.1f\n",
                  stateName(state), homed ? 1 : 0, optActive() ? 1 : 0, Cfg.enable_s_curve ? "ON" : "OFF", v, a, v_goal, A_MAX, J_MAX, v_cmps);
  }
}
