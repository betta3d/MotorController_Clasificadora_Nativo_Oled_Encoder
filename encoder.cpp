#include "encoder.h"
#include "pins.h"
#include <Arduino.h>

namespace App {

// ---------- Config ----------
// El debounce por hardware/software ya es suficiente.
// static constexpr uint32_t DEBOUNCE_US = 400;

// ---------- Estado interno ----------
static volatile int32_t encoder_accumulator = 0;
static volatile uint8_t encoder_state = 0;

// Para convertir “pasos finos” a 1 paso por muesca (detent).
// La mayoría de encoders mecánicos tienen 4 transiciones válidas por detent.
static constexpr int ENCODER_PULSES_PER_STEP = 2;

// Botón (debounce)
static bool     encBtnPrev   = false;
static uint32_t encBtnLastMs = 0;

// ---------- Tabla de estados (Marlin) ----------
// Esta tabla es más compacta y robusta para decodificar la cuadratura.
// Usa "full-step" (un estado por cada transición A/B)
static const int8_t KNOB_DIR[] = {
  0, -1,  1,  0,
  1,  0,  0, -1,
 -1,  0,  0,  1,
  0,  1, -1,  0
};

// ---------- ISR (ambos canales) ----------
static void IRAM_ATTR enc_isr() {
  // Lee los pines A y B y actualiza el estado del encoder
  encoder_state = (encoder_state << 2) | (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  // Acumula el resultado de la tabla de transiciones
  // El estado (4 bits) se usa como índice en la tabla KNOB_DIR
  encoder_accumulator += KNOB_DIR[encoder_state & 0x0f];
}

void encoderInit() {
  // Pines de rotación con pull-up interno del ESP32
  pinMode(PIN_ENC_A,   INPUT_PULLUP);
  pinMode(PIN_ENC_B,   INPUT_PULLUP);
  // Pin de botón como entrada simple (el módulo tiene su propia resistencia pull-down)
  pinMode(PIN_ENC_BTN, INPUT_PULLUP);

  // Estado inicial
  encoder_state = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);

  // Interrupciones por cambio en ambos canales
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), enc_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), enc_isr, CHANGE);
}

int8_t encoderReadDelta() {
  int32_t acc_val;

  // Lee y consume los pasos acumulados de forma atómica
  noInterrupts();
  acc_val = encoder_accumulator;
  // "Consumimos" los pulsos correspondientes a los detents completos
  int32_t detents = acc_val / ENCODER_PULSES_PER_STEP;
  encoder_accumulator -= detents * ENCODER_PULSES_PER_STEP;
  interrupts();

  return (int8_t)detents;
}

bool encoderReadClick() {
  // El módulo es activo en alto (botón presionado = HIGH)
  bool pressed = (digitalRead(PIN_ENC_BTN) == HIGH);
  uint32_t now = millis();
  bool click = false;
  // flanco de liberación + debounce simple
  if (!pressed && encBtnPrev && (now - encBtnLastMs) > 50) { // Aumentado a 50ms para mejor debounce
    click = true;
  }
  if (pressed != encBtnPrev) {
    encBtnPrev = pressed;
    encBtnLastMs = now;
  }
  return click;
}

} // namespace App
