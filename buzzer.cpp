#include "buzzer.h"
#include "pins.h"
#include <Arduino.h>
#include "servo_manager.h"

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
  // Reutilizamos mismo estado para beepBack (simple); diferenciamos solo frecuencia
  // Estado melody startup
  static bool melodyActive = false;
  static uint8_t melodyIndex = 0; // 0..N
  static uint32_t melodyNextMs = 0;
  struct Note { uint16_t freq; uint16_t dur; uint16_t gap; };
  // Pequeña escala ascendente (puedes ajustar a gusto)
  // MELODY de arranque:
  // Notas escogidas: Do6, Mi6, Sol6 (tríada mayor de C) ascendente.
  // Frecuencias aproximadas estándar temperamento igual:
  //   Do6 (C6)  = 1046.50 Hz (~1047)
  //   Mi6 (E6)  = 1318.51 Hz (~1319)
  //   Sol6 (G6) = 1567.98 Hz (~1568)
  // Duraciones: primeras dos cortas, última un poco más larga para cierre.
  static const Note MELODY[] = {
    { 1047, 90, 40 },  // Do6  (C6)
    { 1319, 90, 40 },  // Mi6  (E6)
    { 1568, 130, 0 }   // Sol6 (G6) cierre
  };
  static const uint8_t MELODY_LEN = sizeof(MELODY)/sizeof(MELODY[0]);

  void init(){
    if (initialized) return;
    pinMode(PIN_BUZZER, OUTPUT);
    initialized = true;
  }

  void beep(uint16_t freqHz, uint16_t ms, uint8_t volume){
    if (!initialized) init();
    if (freqHz < 50) freqHz = 50; if (freqHz > 10000) freqHz = 10000;
    (void)volume; // volumen no aplicable con tone()
    // Evitar interferir con servo en vivo (uso de timers LEDC por tone)
    if (App::ServoMgr.live()) return;
    tone(PIN_BUZZER, freqHz, ms);
    delay(ms);
    noTone(PIN_BUZZER);
  }

  void beepNav(){
    if (!initialized) init();
    if (App::ServoMgr.live()) return; // no interferir en Live
    navFreq = BUZZER_NAV_FREQ;
    const uint16_t dur = BUZZER_NAV_MS;
    tone(PIN_BUZZER, navFreq, dur);
    navEndMs = millis() + dur + 5;  // colchón
    navActive = true;
  }

  void beepBack(){
    if (!initialized) init();
    if (App::ServoMgr.live()) return; // no interferir en Live
    // Frecuencia un poco mas aguda para diferenciar
    navFreq = BUZZER_NAV_FREQ + 800; // +800 Hz (~4600 Hz)
    const uint16_t dur = BUZZER_NAV_MS;
    tone(PIN_BUZZER, navFreq, dur);
    navEndMs = millis() + dur + 5;
    navActive = true;
  }

  void beepSave(){
    // Guardamos la semántica bloqueante para diferenciar claramente (confirmación fuerte)
    beep(2600, 80, 220);
  }

  void beepError(){
    // Tres pips rápidos de error; bloqueante intencional para enfatizar
    if (!initialized) init();
    if (App::ServoMgr.live()) return; // no interferir en Live
    const uint16_t f = 1800; // frecuencia más grave que nav/back
    for (int i=0;i<3;i++){
      tone(PIN_BUZZER, f, 45);
      delay(55);
      noTone(PIN_BUZZER);
      // pequeña pausa entre pips (ya cubierta por delay vs dur)
    }
  }

  void startStartupMelody(){
    if (!initialized) init();
    melodyActive = true;
    melodyIndex = 0;
    melodyNextMs = 0; // ejecutar inmediata
  }

  void update(){
    uint32_t now = millis();
    // Gestion nav beep
    if (navActive && (int32_t)(now - navEndMs) >= 0){
      noTone(PIN_BUZZER);
      navActive = false;
    }
    // Gestion melody
    if (melodyActive){
      if (melodyIndex >= MELODY_LEN){
        melodyActive = false; return;
      }
      if ((int32_t)(now - melodyNextMs) >= 0){
        const Note& N = MELODY[melodyIndex];
        tone(PIN_BUZZER, N.freq, N.dur);
        melodyNextMs = now + N.dur + N.gap;
        melodyIndex++;
      }
    }
  }
}
