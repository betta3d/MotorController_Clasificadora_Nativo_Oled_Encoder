#include "buzzer.h"
#include "pins.h"
#include <Arduino.h>

// Diseño hardware sugerido:
// Usar un buzzer pasivo o piezo entre PIN_BUZZER y GND.
// Recomendado colocar una resistencia serie ~100-220 ohms para limitar corriente de picos y amortiguar.
// Si se usa buzzer activo (ya genera tono), solo hacer HIGH/LOW con delays y omitir frecuencia.
// Aquí implementamos generación simple por tone() (Arduino core ESP32 provee ledcAttachChannel internamente).

namespace Buzzer {
  static bool initialized = false;
  // Estado para beepNav no bloqueante
  static bool navActive = false;
  static uint32_t navEndMs = 0;
  static uint16_t navFreq = 0;

  void init(){
    if (initialized) return;
    pinMode(PIN_BUZZER, OUTPUT);
    initialized = true;
  }

  void beep(uint16_t freqHz, uint16_t ms, uint8_t volume){
    if (!initialized) init();
    if (freqHz < 50) freqHz = 50; if (freqHz > 10000) freqHz = 10000;
    (void)volume; // volumen no aplicable con tone()
    tone(PIN_BUZZER, freqHz, ms);
    delay(ms);
    noTone(PIN_BUZZER);
  }

  void beepNav(){
    if (!initialized) init();
    navFreq = BUZZER_NAV_FREQ;
    const uint16_t dur = BUZZER_NAV_MS;
    tone(PIN_BUZZER, navFreq, dur);
    navEndMs = millis() + dur + 5;  // colchón
    navActive = true;
  }

  void beepSave(){
    // Guardamos la semántica bloqueante para diferenciar claramente (confirmación fuerte)
    beep(2600, 80, 220);
  }

  void update(){
    if (!navActive) return;
    if ((int32_t)(millis() - navEndMs) >= 0){
      noTone(PIN_BUZZER);
      navActive = false;
    }
  }
}
