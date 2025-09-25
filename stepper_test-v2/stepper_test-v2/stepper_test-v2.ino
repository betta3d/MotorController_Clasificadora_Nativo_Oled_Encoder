/*
 * Clasificadora de Huevos — Control por sectores con ISR mínima
 * ESP32 + TMC2208 (Step/Dir) — Prototipo NEMA17 / luego NEMA23
 *
 * Versión: v1.3.8  (2025-09-21)
 *
 * Cambios vs v1.3.7:
 *  - Muevo TODA la lógica pesada (sector, jerk/acc, FF, homing target) a loop() a 1 kHz.
 *  - La ISR del STEP solo genera el pulso, avanza el odómetro y usa next_period_us precomputado.
 *  - Cero prints desde ISR; sin floats pesados en ISR ⇒ adiós Guru Meditation.
 */

#include <Arduino.h>
#include <math.h>

//==================== Configuración mecánica / electrónica ==================
constexpr int   MOTOR_STEPS   = 200;   // 1.8°/paso
constexpr int   MICROSTEPS    = 16;    // TMC2208 a 1/16
constexpr float GEAR_RATIO    = 1.0f;  // Prototipo 1:1; Producción p.ej. 5.0

constexpr int   STEPS_PER_REV = int(MOTOR_STEPS * MICROSTEPS * GEAR_RATIO);
constexpr float STEPS_PER_DEG = float(STEPS_PER_REV) / 360.0f;

// Pines (ajusta a tu cableado)
const int PIN_STEP  = 25;
const int PIN_DIR   = 26;
const int PIN_ENA   = 27;
const int PIN_INDEX = 33;

// Timings mínimos STEP
constexpr uint32_t STEP_PULSE_MIN_US   = 2;
constexpr uint32_t STEP_PERIOD_MIN_US  = 4;

//======================== Sectores angulares (grados) =======================
constexpr float DEG_SLOW_1_A = 355, DEG_SLOW_1_B = 360;
constexpr float DEG_SLOW_2_A =   0, DEG_SLOW_2_B =   5;
constexpr float DEG_SLOW_3_A = 175, DEG_SLOW_3_B = 185;
constexpr float DEG_MED_A    =   5, DEG_MED_B    = 175;
constexpr float DEG_FAST_A   = 185, DEG_FAST_B   = 355;

//=================== Consignas (tuneables por Serial) ======================
volatile float vel_lenta_dps   = 25.0f;   // L=xx.x (°/s)
volatile float vel_media_dps   = 100.0f;  // M=xx.x
volatile float vel_rapida_dps  = 240.0f;  // R=xx.x

volatile float jerk_dps3       = 1000.0f; // J=xx.x  (↑=transición más viva, ↓=más suave)
volatile float acc_max_dps2    = 400.0f;  // A=xx.x  (↑=acelera fuerte, ↓=suave)

volatile float jerk_stop_dps3  = 1200.0f; // JSTOP=xx (frenado suave)
volatile float acc_stop_dps2   = 500.0f;  // ASTOP=xx

// Feed-Forward: false=FF OFF (S-curve), true=FF ON (aplica target inmediato)
volatile bool  feed_forward    = false;   // FF=OFF por defecto

// Índice: ventana y lockout (solo homing)
volatile float index_win_deg     = 10.0f; // WIN=xx (semi-ancho ± 0 relativo)
volatile float index_lockout_deg = 40.0f; // LOCK=xx

// OFFSET y sentido principal
volatile float  offset_deg       = 0.0f;  // OFFSET=±xx.x (sensor→cero mecánico)
volatile int8_t main_dir_sign    = +1;    // DIR=CW(+1) | DIR=CCW(-1)
volatile bool   dir_pin_invert   = false; // DIRPOL=NORM(false) | DIRPOL=INV(true)

// HOMING
volatile float homing_coarse_dps = 40.0f; // HCOARSE=xx
volatile float homing_fine_dps   = 10.0f; // HFINE=xx
volatile float homing_backoff_deg= 8.0f;  // HBACK=xx
volatile float homing_rev_max    = 1.25f; // HREVMAX=xx (vueltas)

//==================== Estado cinemático tiempo real ========================
volatile uint32_t step_out     = 0;    // odómetro [0..STEPS_PER_REV)
volatile float    v_dps        = 0.0f; // velocidad actual (°/s)
volatile float    a_dps2       = 0.0f; // acel actual (°/s²)
volatile float    target_dps   = 0.0f; // consigna (°/s)

hw_timer_t* tmrStep = nullptr;

volatile uint32_t next_period_us = 2000; // >>>>> usado por la ISR para programar el pulso <<<<<
volatile bool     step_state     = false;
volatile int8_t   dir_sign       = +1;   // sentido actual

// Parada suave / control de STEP
volatile bool     stop_requested = false;
volatile bool     stepper_enabled= true;
volatile bool     stopped_flag   = false; // para log en loop()

//===================== Índice: IRQ, captura y filtros ======================
volatile uint32_t idx_irq_raw    = 0;
volatile uint32_t idx_valid      = 0;
volatile uint32_t lastIndexUs    = 0;
volatile uint32_t step_at_irq    = 0;     // info
volatile bool     index_pending  = false;
volatile bool     index_lockout  = false;
volatile uint32_t lockout_clear_step = 0;
constexpr uint32_t INDEX_DEBOUNCE_US = 1500;

// Odómetro RELATIVO de HOMING
volatile int32_t  hom_rel_steps  = 0;     // 0 en inicio de homing y en COARSE OK
volatile int32_t  hom_rel_at_irq = 0;     // captura relativa
volatile int      index_edge_mode = CHANGE; // IDX=CHANGE|RISING|FALLING

//======================== Máquina de estados ================================
enum class SysState : uint8_t { STARTUP, HOMING, HOMED, RUNNING, FAULT };
volatile SysState state = SysState::STARTUP;
const char* state_str(SysState s){
  switch(s){
    case SysState::STARTUP: return "STARTUP";
    case SysState::HOMING:  return "HOMING";
    case SysState::HOMED:   return "HOMED";
    case SysState::RUNNING: return "RUNNING";
    case SysState::FAULT:   return "FAULT";
  } return "?";
}
void state_set(SysState s){ state = s; Serial.printf("[STATE] %s\n", state_str(s)); }

//======================== Sectores: clase utilitaria ========================
enum Sector : uint8_t { SECTOR_LENTO=0, SECTOR_MEDIO=1, SECTOR_RAPIDO=2 };
struct SectorUtil {
  static inline Sector of(uint32_t step_idx){
    float deg = (float(step_idx) / float(STEPS_PER_REV)) * 360.0f;
    if ((deg >= DEG_SLOW_1_A && deg < DEG_SLOW_1_B) ||
        (deg >= DEG_SLOW_2_A && deg < DEG_SLOW_2_B) ||
        (deg >= DEG_SLOW_3_A && deg < DEG_SLOW_3_B)) return SECTOR_LENTO;
    if (deg >= DEG_MED_A && deg < DEG_MED_B) return SECTOR_MEDIO;
    if (deg >= DEG_FAST_A && deg < DEG_FAST_B) return SECTOR_RAPIDO;
    return SECTOR_LENTO;
  }
  static inline const char* to_str(Sector s){
    switch(s){ case SECTOR_LENTO: return "LENTO"; case SECTOR_MEDIO: return "MEDIO"; case SECTOR_RAPIDO: return "RAPIDO"; }
    return "?";
  }
};

//============================ Utilidades ===================================
inline float wrap360(float d){ while(d>=360.0f) d-=360.0f; while(d<0.0f) d+=360.0f; return d; }
inline float deg_from_step(uint32_t s){ return (float(s) / float(STEPS_PER_REV)) * 360.0f; }
inline float deg_rel_from_steps(int32_t rel){ return (float(rel) / float(STEPS_PER_REV)) * 360.0f; }
inline float arc_len(float a, float b){ return (b >= a) ? (b - a) : (360.0f - a + b); }
inline uint32_t deg_to_steps(float deg){ return (uint32_t) lroundf(fmaxf(0.0f, deg) * STEPS_PER_DEG); }
uint32_t dps_to_period_us(float dps){
  if (dps <= 0.0f) return 1000000; // periodo grande “casi parado”
  float sps = fmaxf(1.0f, dps * STEPS_PER_DEG);
  uint32_t per = (uint32_t)(1e6f / sps);
  if (per < STEP_PERIOD_MIN_US) per = STEP_PERIOD_MIN_US;
  return per;
}
float rpm_out_approx(){
  float vL = vel_lenta_dps, vM = vel_media_dps, vR = vel_rapida_dps;
  float slow_deg = arc_len(DEG_SLOW_1_A,DEG_SLOW_1_B)+arc_len(DEG_SLOW_2_A,DEG_SLOW_2_B)+arc_len(DEG_SLOW_3_A,DEG_SLOW_3_B);
  float med_deg  = arc_len(DEG_MED_A,DEG_MED_B);
  float fast_deg = arc_len(DEG_FAST_A,DEG_FAST_B);
  float t_slow = slow_deg / fmaxf(1.0f, vL);
  float t_med  = med_deg  / fmaxf(1.0f, vM);
  float t_fast = fast_deg / fmaxf(1.0f, vR);
  float T = t_slow + t_med + t_fast;
  return (T>0)? (60.0f / T) : 0.0f;
}
inline void writeDirPin(){
  bool level = (dir_sign > 0) ? HIGH : LOW;
  if (dir_pin_invert) level = !level;
  digitalWrite(PIN_DIR, level);
}
inline void setDirection(int8_t sign){ dir_sign = (sign>=0)?+1:-1; writeDirPin(); }
inline uint32_t step_advance(uint32_t s, int8_t sign){ if (sign>0){ s++; if (s>= (uint32_t)STEPS_PER_REV) s=0; } else { s = (s==0)? (STEPS_PER_REV-1) : (s-1); } return s; }

bool in_centered_window(float deg_rel, float win){
  deg_rel = wrap360(deg_rel);
  float lo = wrap360(0.0f - win), hi = wrap360(0.0f + win);
  if (lo <= hi) return (deg_rel >= lo && deg_rel <= hi);
  return (deg_rel >= lo || deg_rel <= hi);
}

//========================== STOP (parada suave) ============================
inline void requestSmoothStop(){ stop_requested = true; }

//============================ ISRs / Timers ================================
void IRAM_ATTR onIndex(){
  uint32_t now = micros();
  if (now - lastIndexUs < INDEX_DEBOUNCE_US) return;
  lastIndexUs = now;
  idx_irq_raw++;
  step_at_irq = step_out;
  hom_rel_at_irq = hom_rel_steps;
  index_pending = true;
}

void IRAM_ATTR onStepTimer(){
  if (!stepper_enabled){
    digitalWrite(PIN_STEP, LOW);
    return;
  }
  // Pulso STEP
  digitalWrite(PIN_STEP, step_state);
  step_state = !step_state;

  if (!step_state){
    // Flanco de caída: avanzar posición y programar próximo periodo
    step_out = step_advance(step_out, dir_sign);
    if (state == SysState::HOMING){
      if (dir_sign > 0) hom_rel_steps++;
      else              hom_rel_steps--;
    }
    uint32_t per_half = next_period_us >> 1;
    if (per_half < STEP_PULSE_MIN_US) per_half = STEP_PULSE_MIN_US;
    timerAlarm(tmrStep, per_half, true, 0);
  }
}

//============================== Homing =====================================
bool doHoming(uint32_t guard_timeout_ms = 15000){
  if (!stepper_enabled){ stepper_enabled = true; timerStart(tmrStep); }
  stop_requested = false; stopped_flag = false;

  state_set(SysState::HOMING);
  index_lockout = false; index_pending = false;
  hom_rel_steps = 0;

  float jerk_save = jerk_dps3, acc_save = acc_max_dps2;
  jerk_dps3 = fmaxf(400.0f, jerk_dps3 * 0.6f);
  acc_max_dps2 = fmaxf(200.0f, acc_max_dps2 * 0.6f);

  const int32_t steps_fault = (int32_t) lroundf(float(STEPS_PER_REV) * homing_rev_max);

  // (1) COARSE
  setDirection(main_dir_sign);
  float vcoarse = (homing_coarse_dps<5.0f)? 5.0f : homing_coarse_dps;
  target_dps = vcoarse;
  uint32_t t0 = millis();
  Serial.printf("[HOMING] COARSE: %s @ %.1f °/s (fault si >%.2f v)\n",
                (main_dir_sign>0?"CW":"CCW"), vcoarse, homing_rev_max);

  while (true){
    if (index_pending){
      index_pending = false;
      step_out = 0; hom_rel_steps = 0;
      idx_valid++;
      index_lockout = true; lockout_clear_step = deg_to_steps(index_lockout_deg);
      Serial.println("[HOMING] COARSE OK -> cero temporal (step_out=0, deg_rel=0)");
      break;
    }
    if (abs(hom_rel_steps) >= steps_fault){
      Serial.printf("[HOMING] FAULT: sin índice en COARSE tras %.2f v\n",
                    fabsf(deg_rel_from_steps(hom_rel_steps))/360.0f);
      state_set(SysState::FAULT); requestSmoothStop();
      jerk_dps3=jerk_save; acc_max_dps2=acc_save; return false;
    }
    if (millis() - t0 > guard_timeout_ms){
      Serial.println("[HOMING] FAULT: guard timeout en COARSE");
      state_set(SysState::FAULT); requestSmoothStop();
      jerk_dps3=jerk_save; acc_max_dps2=acc_save; return false;
    }
    delay(1);
  }
  index_lockout = false; index_pending = false;

  // (2) BACKOFF (25% de vcoarse, máx 30 °/s)
  float back_deg = (homing_backoff_deg<3.0f)? 3.0f : homing_backoff_deg;
  uint32_t back_steps_target = deg_to_steps(back_deg);
  int32_t hom_rel_at_back_start = hom_rel_steps;

  setDirection(-main_dir_sign);
  float vback = fminf(fmaxf(2.0f, vcoarse * 0.25f), 30.0f);
  target_dps = vback;
  Serial.printf("[HOMING] BACKOFF: %s %.1f° (steps≈%lu) @ %.1f °/s (25%% de COARSE)\n",
                (main_dir_sign>0?"CCW":"CW"),
                back_deg, (unsigned long)back_steps_target, vback);

  while ((uint32_t)abs(hom_rel_steps - hom_rel_at_back_start) < back_steps_target) { delay(1); }

  // (3) FINE: 0±WIN relativos
  setDirection(main_dir_sign);
  float vfina = (homing_fine_dps<2.0f)? 2.0f : homing_fine_dps;
  target_dps = vfina;
  index_lockout = false; index_pending = false;
  t0 = millis();
  Serial.printf("[HOMING] FINE: %s @ %.1f °/s, win=±%.1f°, offset=%.2f° (fault si >%.2f v)\n",
                (main_dir_sign>0?"CW":"CCW"), vfina, index_win_deg, offset_deg, homing_rev_max);

  int32_t hom_rel_at_fine_start = hom_rel_steps;

  while (true){
    if (index_pending){
      index_pending = false;
      float deg_rel = wrap360(deg_rel_from_steps(hom_rel_at_irq));
      bool ok = in_centered_window(deg_rel, index_win_deg);
      if (ok){
        long off_steps = lroundf(offset_deg * STEPS_PER_DEG); long N = STEPS_PER_REV;
        uint32_t preload = (main_dir_sign > 0)
          ? (uint32_t)(( (N - off_steps) % N + N ) % N)      // CW
          : (uint32_t)(( off_steps % N + N ) % N);           // CCW
        step_out = preload;
        idx_valid++;
        index_lockout = true; lockout_clear_step = deg_to_steps(index_lockout_deg);
        Serial.printf("[HOMING] FINE OK: deg_rel=%.2f → preload=%lu steps (cero mecánico). Pausa 2 s...\n",
                      deg_rel, (unsigned long)preload);
        float save_t = target_dps; target_dps = 0.0f;
        unsigned long p0 = millis(); while (millis()-p0 < 2000) delay(1);
        target_dps = save_t;
        jerk_dps3=jerk_save; acc_max_dps2=acc_save;
        state_set(SysState::HOMED);
        return true;
      } else {
        Serial.printf("[HOMING] FINE: fuera de ventana (deg_rel=%.2f)\n", deg_rel);
      }
    }
    if (abs(hom_rel_steps - hom_rel_at_fine_start) >= steps_fault){
      Serial.printf("[HOMING] FAULT: sin índice válido en FINE tras %.2f v\n",
                    fabsf(deg_rel_from_steps(hom_rel_steps - hom_rel_at_fine_start))/360.0f);
      state_set(SysState::FAULT); requestSmoothStop();
      jerk_dps3=jerk_save; acc_max_dps2=acc_save; return false;
    }
    if (millis() - t0 > guard_timeout_ms){
      Serial.println("[HOMING] FAULT: guard timeout en FINE");
      state_set(SysState::FAULT); requestSmoothStop();
      jerk_dps3=jerk_save; acc_max_dps2=acc_save; return false;
    }
    delay(1);
  }
}

//================================ Setup ====================================
void setup(){
  Serial.begin(115200); delay(300);

  pinMode(PIN_STEP, OUTPUT);
  pinMode(PIN_DIR,  OUTPUT);
  pinMode(PIN_ENA,  OUTPUT);
  pinMode(PIN_INDEX, INPUT_PULLUP);

  digitalWrite(PIN_STEP, LOW);
  setDirection(+1);
  digitalWrite(PIN_ENA, LOW); // habilita el driver

  attachInterrupt(digitalPinToInterrupt(PIN_INDEX), onIndex, index_edge_mode);

  state_set(SysState::STARTUP);

  // Timer STEP a 1 MHz
  tmrStep = timerBegin(1000000);
  timerAttachInterrupt(tmrStep, &onStepTimer);

  next_period_us = dps_to_period_us(fmaxf(1.0f, vel_lenta_dps)); // arranque
  uint32_t per_half = (next_period_us>>1);
  if (per_half < STEP_PULSE_MIN_US) per_half = STEP_PULSE_MIN_US;
  timerAlarm(tmrStep, per_half, true, 0);
  timerStart(tmrStep);

  // Homing inicial
  if (doHoming()){
    state_set(SysState::RUNNING);
    setDirection(main_dir_sign);
    target_dps = vel_lenta_dps;
  }

  Serial.printf("FW v1.3.8 | DIR=%s DIRPOL=%s OFFSET=%.2f° WIN=±%.1f° LOCK=%.1f° HREVMAX=%.2f FF=%s\n",
                (main_dir_sign>0?"CW":"CCW"), (dir_pin_invert?"INV":"NORM"),
                offset_deg, index_win_deg, index_lockout_deg, homing_rev_max, (feed_forward?"ON":"OFF"));
  Serial.println(F("Comandos: DIR=CW|CCW  DIRPOL=NORM|INV  OFFSET=..  WIN=..  LOCK=..  HREVMAX=.."));
  Serial.println(F("          L=.. M=.. R=..  J=.. A=..  JSTOP=.. ASTOP=.."));
  Serial.println(F("          HCOARSE=.. HFINE=.. HBACK=.."));
  Serial.println(F("          IDX=CHANGE|RISING|FALLING  HOME!  STOP!  FF=ON|FF=OFF  STATE?"));
}

//=============================== Loop ======================================
void loop(){
  static uint32_t last_hb = 0;
  static Sector   last_sector = SECTOR_LENTO;

  // --- “Control loop” soft: 1 kHz -----------------
  static uint32_t last_us = micros();
  uint32_t now = micros();
  if ((int32_t)(now - last_us) >= 1000){ // ~1 ms
    float dt = (now - last_us) * 1e-6f;
    if (dt < 0.0005f) dt = 0.0005f;
    if (dt > 0.01f)   dt = 0.01f;
    last_us = now;

    // 1) Target por estado / sector
    if (state == SysState::FAULT || stop_requested){
      target_dps = 0.0f;
    } else if (state == SysState::HOMING){
      // el homing ya setea target_dps (coarse/backoff/fine)
    } else if (state == SysState::HOMED || state == SysState::RUNNING){
      Sector s = SectorUtil::of(step_out);
      if      (s == SECTOR_LENTO)  target_dps = vel_lenta_dps;
      else if (s == SECTOR_MEDIO)  target_dps = vel_media_dps;
      else                         target_dps = vel_rapida_dps;
    } else {
      target_dps = 0.0f;
    }

    // 2) FF o S-curve (suave)
    if (feed_forward){
      v_dps  = target_dps;
      a_dps2 = 0.0f;
    } else {
      float J = (state == SysState::FAULT || stop_requested) ? jerk_stop_dps3 : jerk_dps3;
      float A = (state == SysState::FAULT || stop_requested) ? acc_stop_dps2  : acc_max_dps2;

      float v_err = target_dps - v_dps;
      const float Kp = 4.0f;
      float a_des = Kp * v_err;
      if (a_des >  A) a_des =  A;
      if (a_des < -A) a_des = -A;
      float da = a_des - a_dps2;
      float da_max = J * dt;
      if      (da >  da_max) a_dps2 += da_max;
      else if (da < -da_max) a_dps2 -= da_max;
      else                   a_dps2  = a_des;
      v_dps += a_dps2 * dt;
      if (fabsf(v_err) < 0.05f && fabsf(a_dps2) < 0.5f) { v_dps = target_dps; a_dps2 = 0.0f; }
    }

    // 3) Re-index por vuelta en HOMED/RUNNING (OFFSET)
    if ((state == SysState::HOMED || state == SysState::RUNNING) && index_pending){
      index_pending = false;
      if (!index_lockout){
        long off_steps = lroundf(offset_deg * STEPS_PER_DEG); long N = STEPS_PER_REV;
        uint32_t preload = (main_dir_sign > 0)
          ? (uint32_t)(( (N - off_steps) % N + N ) % N)      // CW
          : (uint32_t)(( off_steps % N + N ) % N);           // CCW
        step_out = preload;
        idx_valid++;
        index_lockout = true; lockout_clear_step = deg_to_steps(index_lockout_deg);
      }
    }
    if (index_lockout && step_out >= lockout_clear_step) index_lockout = false;

    // 4) Programar periodo para la ISR
    float v_safe = v_dps;
    if (!(state == SysState::FAULT || stop_requested)){
      if (v_safe < 0.1f) v_safe = 0.1f;
    }
    next_period_us = dps_to_period_us(v_safe);

    // 5) Parada suave: cuando v≈0, apagar STEP
    if ((state == SysState::FAULT || stop_requested) && stepper_enabled){
      if (fabsf(v_dps) < 0.02f && fabsf(a_dps2) < 0.1f){
        stepper_enabled = false; stopped_flag = true;
        timerStop(tmrStep);
        digitalWrite(PIN_STEP, LOW);
        v_dps = 0.0f; a_dps2 = 0.0f;
      }
    }
  }
  // -----------------------------------------------

  if (stopped_flag){ stopped_flag = false; Serial.println("[STOP] Motor detenido (timer STEP apagado)"); }

  if (millis() - last_hb >= 1000){
    last_hb = millis();
    float rpm = rpm_out_approx();
    Serial.printf("[HB] state=%s sector=%s dir=%s idx_raw=%lu idx_valid=%lu step_out=%lu rpm≈%.2f  v=%.1f tgt=%.1f per=%luus\n",
      state_str(state), SectorUtil::to_str(SectorUtil::of(step_out)), (dir_sign>0?"CW":"CCW"),
      (unsigned long)idx_irq_raw, (unsigned long)idx_valid, (unsigned long)step_out, rpm,
      v_dps, target_dps, (unsigned long)next_period_us);
  }

  Sector s = SectorUtil::of(step_out);
  static Sector last_sector = SECTOR_LENTO;
  if (s != last_sector){ last_sector = s; Serial.printf("[SECTOR] %s\n", SectorUtil::to_str(s)); }

  if (Serial.available()){
    String line = Serial.readStringUntil('\n'); line.trim(); float v=0;

    if      (line.equalsIgnoreCase("DIR=CW"))  { main_dir_sign=+1; setDirection(main_dir_sign); Serial.println(F("DIR=CW")); }
    else if (line.equalsIgnoreCase("DIR=CCW")) { main_dir_sign=-1; setDirection(main_dir_sign); Serial.println(F("DIR=CCW")); }

    else if (line.equalsIgnoreCase("DIRPOL=NORM")){ dir_pin_invert=false; writeDirPin(); Serial.println(F("DIRPOL=NORM")); }
    else if (line.equalsIgnoreCase("DIRPOL=INV")) { dir_pin_invert=true;  writeDirPin(); Serial.println(F("DIRPOL=INV")); }

    else if (line.startsWith("OFFSET=")) { offset_deg = line.substring(7).toFloat(); Serial.printf("OFFSET=%.2f°\n", offset_deg); }
    else if (line.startsWith("WIN=")   && (v=line.substring(4).toFloat())>0){ index_win_deg=fminf(45.0f,fmaxf(1.0f,v)); Serial.printf("WIN=±%.1f°\n", index_win_deg); }
    else if (line.startsWith("LOCK=")  && (v=line.substring(5).toFloat())>0){ index_lockout_deg=fminf(120.0f,fmaxf(10.0f,v)); Serial.printf("LOCKOUT=%.1f°\n", index_lockout_deg); }
    else if (line.startsWith("HREVMAX=") && (v=line.substring(8).toFloat())>0){ homing_rev_max=v; Serial.printf("HREVMAX=%.2f\n", homing_rev_max); }

    else if (line.startsWith("L=")     && (v=line.substring(2).toFloat())>0){ vel_lenta_dps=v; }
    else if (line.startsWith("M=")     && (v=line.substring(2).toFloat())>0){ vel_media_dps=v; }
    else if (line.startsWith("R=")     && (v=line.substring(2).toFloat())>0){ vel_rapida_dps=v; }
    else if (line.startsWith("J=")     && (v=line.substring(2).toFloat())>0){ jerk_dps3=v; }
    else if (line.startsWith("A=")     && (v=line.substring(2).toFloat())>0){ acc_max_dps2=v; }
    else if (line.startsWith("JSTOP=") && (v=line.substring(6).toFloat())>0){ jerk_stop_dps3=v; }
    else if (line.startsWith("ASTOP=") && (v=line.substring(6).toFloat())>0){ acc_stop_dps2=v; }

    else if (line.startsWith("HCOARSE=") && (v=line.substring(8).toFloat())>0){ homing_coarse_dps=v; Serial.printf("HCOARSE=%.1f\n", homing_coarse_dps); }
    else if (line.startsWith("HFINE=")   && (v=line.substring(6).toFloat())>0){ homing_fine_dps=v;   Serial.printf("HFINE=%.1f\n",   homing_fine_dps); }
    else if (line.startsWith("HBACK=")   && (v=line.substring(6).toFloat())>0){ homing_backoff_deg=v;Serial.printf("HBACK=%.1f°\n",  homing_backoff_deg); }

    else if (line.equalsIgnoreCase("IDX=CHANGE"))  { detachInterrupt(digitalPinToInterrupt(PIN_INDEX)); index_edge_mode=CHANGE;  attachInterrupt(digitalPinToInterrupt(PIN_INDEX), onIndex, index_edge_mode); Serial.println(F("Index edge -> CHANGE")); }
    else if (line.equalsIgnoreCase("IDX=RISING"))  { detachInterrupt(digitalPinToInterrupt(PIN_INDEX)); index_edge_mode=RISING;  attachInterrupt(digitalPinToInterrupt(PIN_INDEX), onIndex, index_edge_mode); Serial.println(F("Index edge -> RISING")); }
    else if (line.equalsIgnoreCase("IDX=FALLING")) { detachInterrupt(digitalPinToInterrupt(PIN_INDEX)); index_edge_mode=FALLING; attachInterrupt(digitalPinToInterrupt(PIN_INDEX), onIndex, index_edge_mode); Serial.println(F("Index edge -> FALLING")); }

    else if (line.equalsIgnoreCase("FF=ON"))  { feed_forward=true;  Serial.println(F("FF=ON (aplica target inmediato, sin rampas)")); }
    else if (line.equalsIgnoreCase("FF=OFF")) { feed_forward=false; Serial.println(F("FF=OFF (S-curve activa)")); }

    else if (line=="HOME!"){
      if (doHoming()){
        state_set(SysState::HOMED);
        state_set(SysState::RUNNING);
        setDirection(main_dir_sign);
        target_dps = vel_lenta_dps;
      }
    }
    else if (line=="STOP!"){ requestSmoothStop(); Serial.println("[STOP] Solicitada parada suave"); }
    else if (line=="STATE?"){ Serial.printf("Estado actual: %s\n", state_str(state)); }
  }

  if (state == SysState::FAULT){ target_dps = 0.0f; }
}
