#include "io.h"
#include "pins.h"
#include "globals.h"
#include <Wire.h>

namespace App {

void ioInit() {
  // Señales TB6600
  pinMode(PIN_STEP, OUTPUT);
  pinMode(PIN_DIR,  OUTPUT);
  pinMode(PIN_ENA,  OUTPUT);
  digitalWrite(PIN_ENA, LOW); // habilitar (común en TB6600 con cátodo común)

  // Entradas
  pinMode(PIN_OPT_HOME,  INPUT);
  pinMode(PIN_BTN_HOME,  INPUT_PULLUP);
  pinMode(PIN_BTN_START, INPUT_PULLUP);

  // LEDs
  pinMode(PIN_LED_ROJO,  OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  digitalWrite(PIN_LED_ROJO,  HIGH);
  digitalWrite(PIN_LED_VERDE, LOW);

  // I2C OLED
  Wire.begin(I2C_SDA, I2C_SCL);
}

void updateLeds(SysState st, bool homedNow) {
  bool rojo=false, verde=false;
  switch (st) {
    case SysState::READY:     rojo = !homedNow; verde = homedNow; break;
    case SysState::UNHOMED:
    case SysState::HOMING_SEEK:
    case SysState::HOMING_BACKOFF:
    case SysState::HOMING_REAPP:
    case SysState::STOPPING:
    case SysState::FAULT:     rojo = true;      verde = false;    break;
    case SysState::RUNNING: {
      uint32_t now = millis();
      if (now - ledBlinkLastMs >= 250) {
        ledVerdeBlinkState = !ledVerdeBlinkState;
        ledBlinkLastMs = now;
      }
      verde = ledVerdeBlinkState; rojo = false; break;
    }
  }
  digitalWrite(PIN_LED_ROJO,  rojo ? HIGH : LOW);
  digitalWrite(PIN_LED_VERDE, verde ? HIGH : LOW);
}

} // namespace App
