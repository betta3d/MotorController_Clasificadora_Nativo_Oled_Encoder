#include "oled_ui.h"
#include <ctype.h>
#include <string.h>
#include "globals.h"
#include "motion.h"
#include "eeprom_store.h"
#include "logger.h"
#include "homing.h"
#include "ui_menu_model.h"
#include "buzzer.h"
#include "wifi_manager.h" // añadido arriba para evitar problemas de namespaces std dentro App
#include "encoder.h"
// Splash ahora se genera tipográficamente (sin bitmap fijo)

namespace App {

// -------------------- OLED INSTANCE --------------------
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ SCL, /* data=*/ SDA
);

static bool splashActive = true;
static uint32_t splashEndMs = 0; // ya no usado directamente, mantenido por compatibilidad
// Nueva animación: fases
enum class SplashPhase { TYPE_TITLE, PAUSE_BETWEEN, TYPE_SUB, MELODY_HOLD, DONE };
static SplashPhase splashPhase = SplashPhase::TYPE_TITLE;
static uint32_t splashPhaseStart = 0;
static const char* SPLASH_TITLE = "Orbis";
static const char* SPLASH_SUB_FULL = "control by Betta";
static uint8_t typedChars = 0;           // cuenta actual de caracteres (reutilizada por ambas líneas)
static uint32_t lastTypeMs = 0;
static const uint32_t TYPE_INTERVAL_MS = 90; // velocidad de tipeo
static uint32_t pauseStartMs = 0;        // inicio pausa entre líneas
static const uint32_t PAUSE_BETWEEN_MS = 1000; // 1 segundo entre líneas
static uint32_t holdStartMs = 0;         // inicio del hold final
static const uint32_t HOLD_MS = 2000;    // 3 segundos después de melody
static bool secondLineStarted = false;

// -------------------- UI CONFIG --------------------
static const uint8_t MARGIN_X   = 4;
static const uint8_t LINE_STEP  = 11;
static const uint8_t FONT_H     = 10;

// Screensaver legacy eliminado: ahora se mostrará ACTION_EXEC o STATUS permanentemente.

// Nombres cortos y numerados para que quepan en una línea
// ===== NUEVO SISTEMA: Helpers de dibujo =====
using namespace App;

// -------------------- EDITABLE PARAMS --------------------
struct ParamRef {
  const char* name;
  float*      pValue;
  float       minV, maxV, step;
  const char* unit;
};

static ParamRef editParams[] = {
  {"Vslow",   &Cfg.v_slow_cmps,  0.1f,  50.0f,   0.1f,  "cm/s"},
  {"Vmed",    &Cfg.v_med_cmps,   0.1f, 100.0f,   0.1f,  "cm/s"},
  {"Vfast",   &Cfg.v_fast_cmps,  0.1f, 200.0f,   0.1f,  "cm/s"},
  {"Accel",   &Cfg.accel_cmps2,  1.0f, 5000.0f,  1.0f,  "cm/s^2"},
  {"Jerk",    &Cfg.jerk_cmps3,   1.0f, 50000.0f, 10.0f, "cm/s^3"},
  {"cm/rev",  &Cfg.cm_per_rev,   0.1f, 500.0f,   0.1f,  "cm/rev"},
};

// -------------------- ENCODER SPEED (for EDIT) --------------------
static uint32_t encVelLastMs = 0;
static float    encVelEma    = 0.0;

static inline float enc_update_velocity(int encDelta) {
  const float ALPHA = 0.25f;
  uint32_t now = millis();
  if (encVelLastMs == 0) { encVelLastMs = now; return 0.0f; }
  float dt = (now - encVelLastMs) / 1000.0f;
  encVelLastMs = now;
  if (dt <= 0.0005f) dt = 0.0005f;
  float inst = fabsf((float)encDelta) / dt;
  encVelEma = (1.0f - ALPHA) * encVelEma + ALPHA * inst;
  return encVelEma;
}

static inline float enc_velocity_multiplier(float detents_per_s, int encDeltaAbs) {
  float m = 1.0f;
  if (detents_per_s >=  4.0f) m = 2.0f;
  if (detents_per_s >=  8.0f) m = 4.0f;
  if (detents_per_s >= 16.0f) m = 8.0f;
  if (detents_per_s >= 32.0f) m = 16.0f;
  if (encDeltaAbs >= 2) m *= 1.5f;
  if (encDeltaAbs >= 3) m *= 2.0f;
  return m;
}

// Throttle de beep en scroll
static uint32_t lastScrollBeepMs = 0;
// Throttle para beeps al ajustar valores en edición
static uint32_t lastEditValueBeepMs = 0;
static uint32_t lastEditRangeBeepMs = 0;
static const uint32_t EDIT_BEEP_MIN_INTERVAL_MS = 70; // ms mínimos entre beeps continuos

// -------------------- DRAWING --------------------
void oledInit() {
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
  // Mostrar splash inmediatamente
  splashActive = true;
  splashPhase = SplashPhase::TYPE_TITLE;
  splashPhaseStart = millis();
  typedChars = 1; // Mostrar solo 'O' en el primer frame
  secondLineStarted = false;
}

static void drawSplash(){
  u8g2.clearBuffer();
  // Línea 1 (título) y línea 2 (subtítulo) con tipeo
  if (splashPhase == SplashPhase::TYPE_TITLE || splashPhase == SplashPhase::PAUSE_BETWEEN || splashPhase == SplashPhase::TYPE_SUB || splashPhase == SplashPhase::MELODY_HOLD || splashPhase == SplashPhase::DONE){
    // Título: mostrar todos los chars tipeados correspondientes a la primera palabra
    u8g2.setFont(u8g2_font_logisoso28_tr);
    size_t titleLen = strlen(SPLASH_TITLE);
    size_t showTitle = 0;
    if (splashPhase == SplashPhase::TYPE_TITLE){
      showTitle = typedChars; if (showTitle > titleLen) showTitle = titleLen;
    } else { showTitle = titleLen; }
    char titleBuf[16]; strncpy(titleBuf, SPLASH_TITLE, showTitle); titleBuf[showTitle]='\0';
    int tw = u8g2.getStrWidth(titleBuf);
    int tx = (128 - tw)/2;
    int tBaseline = 36; // un poco más arriba para dejar aire
    u8g2.drawStr(tx, tBaseline, titleBuf);

    // Subtítulo: solo después que la pausa termina
    if (splashPhase == SplashPhase::TYPE_SUB || splashPhase == SplashPhase::MELODY_HOLD || splashPhase == SplashPhase::DONE){
      u8g2.setFont(u8g2_font_6x10_tf);
      size_t subLen = strlen(SPLASH_SUB_FULL);
      size_t charsSub = typedChars; // reutilizamos typedChars pero al iniciar segunda línea se resetea
      if (!secondLineStarted) charsSub = 0; // protección (no debería suceder)
      if (charsSub > subLen) charsSub = subLen;
      char subBuf[48]; strncpy(subBuf, SPLASH_SUB_FULL, charsSub); subBuf[charsSub]='\0';
      int sw = u8g2.getStrWidth(subBuf);
      int sx = (128 - sw)/2;
      u8g2.drawStr(sx, 58, subBuf);
    }
  }
  u8g2.sendBuffer();
}

static void drawStatusScreen() {
  float ang = currentAngleDeg();
  float steps_per_cm = (Cfg.cm_per_rev > 0.0f) ? ((float)stepsPerRev / Cfg.cm_per_rev) : 0.0f;
  float v_cmps = (steps_per_cm > 0.0f) ? (v / steps_per_cm) : 0.0f;
  const char* sect = sectorName(ang);

  u8g2.clearBuffer();
  // WiFi icon estilo arcs + texto estado
  WifiMgr::State ws = WifiMgr::state();
  int cx = MARGIN_X + 12; // centro aproximado icono
  int cy = 12;
  // Dibujar 3 arcos (aproximados con lines) estilo logo WiFi
  auto arc = [&](int r1, int r2){
    // r1 interno, r2 externo. Representamos medio arco inferior usando líneas horizontales de ancho creciente.
    for (int y=0;y<(r2-r1);++y){
      int ry = r1 + y;
      int halfW = (int)( (float)ry * 1.2f ); // factor para expandir
      u8g2.drawHLine(cx - halfW, cy + y, halfW*2+1);
    }
  };
  if (ws == WifiMgr::State::CONNECTED){
    arc(0,2); arc(3,5); arc(6,8);
  } else if (ws == WifiMgr::State::CONNECTING){
    uint32_t phase = (millis()/400)%3; // ilumina arco progresivo
    if (phase>=0) arc(0,2);
    if (phase>=1) arc(3,5);
    if (phase>=2) arc(6,8);
  } else {
    // Disconnected: dibujar arcos + línea diagonal de corte
    arc(0,2); arc(3,5); arc(6,8);
    u8g2.drawLine(cx-10, cy-6, cx+10, cy+8);
  }
  const char* statusTxt = (ws==WifiMgr::State::CONNECTED)?"Conectado": (ws==WifiMgr::State::CONNECTING?"Conectando":"No Conectado");
  u8g2.drawStr(MARGIN_X+28, 10, statusTxt);

  char line[40];
 snprintf(line, sizeof(line), "Estado: %s", stateName(state));
  u8g2.drawStr(MARGIN_X, 22, line);
  snprintf(line, sizeof(line), "Home  : %s", homed ? "SI" : "NO");
  u8g2.drawStr(MARGIN_X, 34, line);
  snprintf(line, sizeof(line), "Ang: %6.2f deg", ang);
  u8g2.drawStr(MARGIN_X, 46, line);
  snprintf(line, sizeof(line), "Vel: %5.1f %s", v_cmps, sect);
  u8g2.drawStr(MARGIN_X, 58, line);

  u8g2.sendBuffer();
}

static bool nodeDisabled(const MenuNode& N){
  if (N.type != MenuNodeType::ACTION) return false;
  // reglas básicas: RUN necesita homed & READY, STOP necesita RUNNING/ROTATING, HOME bloqueado si ya homing en curso
  if (strcmp(N.label,"RUN")==0) return !(homed && state==SysState::READY);
  if (strcmp(N.label,"STOP")==0) return !(state==SysState::RUNNING || state==SysState::ROTATING || state==SysState::STOPPING);
  if (strcmp(N.label,"HOME")==0) return (state==SysState::HOMING_SEEK);
  return false;
}

// Helper para limpiar margen derecho (aplicaremos en todas las pantallas)
static inline void clearRightMargin(){
  u8g2.setDrawColor(0);
  // Reservamos los últimos 8 px como área negra fija (reduce posibilidad de ver columna brillante)
  u8g2.drawBox(120,0,8,64);
  u8g2.setDrawColor(1);
}

static void drawHierMenuList() {
  u8g2.clearBuffer();
  // Encabezado fijo (no debe ser sobrescrito por items)
  if (uiNav.stackCount==0){
    u8g2.drawStr(MARGIN_X, 10, "MENU");
  } else {
    const char* raw = uiNav.stack[uiNav.stackCount-1]->label;
    char up[20]; size_t i=0; for (; i < sizeof(up)-1 && raw[i]; ++i){ char c=raw[i]; if (c>='a'&&c<='z') c = c - 'a' + 'A'; up[i]=c; } up[i]='\0';
    u8g2.drawStr(MARGIN_X,10, up);
  }
  const int total = uiNav.currentCount;
  if (total <= 0){ clearRightMargin(); u8g2.sendBuffer(); return; }

  // Mostramos SOLO 4 ítems para no pelear con el encabezado.
  // Baselines: 22,34,46,58
  static const uint8_t BASELINES4[4] = {22,34,46,58};
  int selected = uiNav.index;
  int first = selected - 1; // intentar centrar con ventana de 4 (dos arriba no cabe por header)
  if (first < 0) first = 0;
  if (first > total - 4) first = max(0, total - 4);
  int last = min(total -1, first + 3);
  // Forzar visibilidad de último si es '<'
  if (last != total -1){
    const MenuNode& ln = uiNav.currentList[total-1];
    if (ln.label && ln.label[0]=='<'){
      last = total -1;
      first = last - 3; if (first < 0) first = 0;
    }
  }
  // Ajuste si selected quedó fuera
  if (selected < first){ first = selected; last = min(total-1, first+3); }
  if (selected > last){ last = selected; first = max(0, last-3); }

  int line = 0;
  for (int i = first; i <= last && line < 4; ++i, ++line){
    uint8_t y = BASELINES4[line];
    const MenuNode& N = uiNav.currentList[i];
    const char* name = N.label ? N.label : "?";
    bool dis = nodeDisabled(N);
    if (i == selected){
      // Box sólo hasta x=119 para dejar margen negro fijo
      u8g2.drawBox(0, y - (FONT_H - 2), 120, FONT_H);
      u8g2.setDrawColor(0);
      if (dis){
        u8g2.drawStr(MARGIN_X, y, "(");
        u8g2.drawStr(MARGIN_X+6, y, name);
        u8g2.drawStr(MARGIN_X+6+u8g2.getStrWidth(name), y, ")");
      } else {
        u8g2.drawStr(MARGIN_X, y, name);
      }
      u8g2.setDrawColor(1);
    } else {
      if (dis){
        u8g2.drawStr(MARGIN_X, y, "(");
        u8g2.drawStr(MARGIN_X+6, y, name);
        u8g2.drawStr(MARGIN_X+6+u8g2.getStrWidth(name), y, ")");
      } else {
        u8g2.drawStr(MARGIN_X, y, name);
      }
    }
  }
  clearRightMargin();
  u8g2.sendBuffer();
}

// Pantalla edición genérica (FLOAT / INT / ENUM) - placeholder simple
// --- Nuevo flujo de edición ---
enum class EditValueSubState { ACTIVE, OPTIONS };
static EditValueSubState editSub = EditValueSubState::ACTIVE;
static float   editSnapshotFloat = 0.f;
static int32_t editSnapshotInt   = 0;
static uint8_t editSnapshotEnum  = 0;
static bool    editValueChanged  = false; // se setea al pasar de ACTIVE -> OPTIONS si varió
static uint8_t optionsIndex = 0;          // 0=OK 1=Editar

static void drawValueEdit(const MenuNode& N) {
  u8g2.clearBuffer();
  u8g2.drawStr(MARGIN_X, 10, N.label);
  char val[48];
  switch (N.type) {
    case MenuNodeType::VALUE_FLOAT: {
      float v = *N.data.vf.ptr; snprintf(val,sizeof(val),"%6.2f %s", v, N.data.vf.unit?N.data.vf.unit:"");
    } break;
    case MenuNodeType::VALUE_INT: {
      int32_t v = *N.data.vi.ptr; snprintf(val,sizeof(val),"%ld %s", (long)v, N.data.vi.unit?N.data.vi.unit:"");
    } break;
    case MenuNodeType::VALUE_ENUM: {
      uint8_t idx = *N.data.ve.ptr; if (idx >= N.data.ve.count) idx = 0; const char* lbl = N.data.ve.labels[idx]; snprintf(val,sizeof(val),"< %s >", lbl);
    } break;
    default: snprintf(val,sizeof(val),"?"); break;
  }
  uint8_t vy = 34;
  uint8_t vw = u8g2.getStrWidth(val);
  if (editSub == EditValueSubState::ACTIVE) {
    u8g2.drawBox(MARGIN_X - 2, vy - (FONT_H - 2), vw + 4, FONT_H);
    u8g2.setDrawColor(0); u8g2.drawStr(MARGIN_X, vy, val); u8g2.setDrawColor(1);
  } else {
    u8g2.drawStr(MARGIN_X, vy, val);
  }
  if (editSub == EditValueSubState::OPTIONS) {
    const char* optOk = "OK"; const char* optEd = "Editar";
    uint8_t y = 58;
    uint8_t xOk = MARGIN_X;
    uint8_t okw = u8g2.getStrWidth(optOk) + 4;
    uint8_t xEd = xOk + okw + 10;
    uint8_t edw = u8g2.getStrWidth(optEd) + 4;
    if (optionsIndex == 0) {
      u8g2.drawBox(xOk-2, y-(FONT_H-2), okw, FONT_H);
      u8g2.setDrawColor(0); u8g2.drawStr(xOk, y, optOk); u8g2.setDrawColor(1);
    } else { u8g2.drawStr(xOk, y, optOk); }
    if (optionsIndex == 1) {
      u8g2.drawBox(xEd-2, y-(FONT_H-2), edw, FONT_H);
      u8g2.setDrawColor(0); u8g2.drawStr(xEd, y, optEd); u8g2.setDrawColor(1);
    } else { u8g2.drawStr(xEd, y, optEd); }
  }
  u8g2.sendBuffer();
}

// Edición de rango angular (start / end / wrap)
// --- Nuevo flujo RANGE_DEG: ACTIVE (edita Start/End/Wrap secuencial) --> OPTIONS (OK / Editar) ---
static const MenuNode* rangeEditingNode = nullptr;
enum class EditRangeSubState { ACTIVE, OPTIONS };
static EditRangeSubState rangeSub = EditRangeSubState::ACTIVE;
static uint8_t rangeFocus = 0; // 0=start 1=end 2=wrap
static float rangeSnapStart = 0.f, rangeSnapEnd = 0.f; static bool rangeSnapWrap = false; // snapshots para guardado condicional
static bool rangeChanged = false;
static uint8_t rangeOptionsIndex = 0; // 0=OK 1=Editar

static void drawRangeEdit(const MenuNode& N){
  u8g2.clearBuffer();
  u8g2.drawStr(MARGIN_X,10,N.label);
  float start = *N.data.rd.startPtr;
  float end   = *N.data.rd.endPtr;
  bool wraps  = *N.data.rd.wrapsPtr;
  char line[48];

  // Líneas
  snprintf(line,sizeof(line),"Start: %6.1f", start);
  if (rangeSub==EditRangeSubState::ACTIVE && rangeFocus==0){
    uint8_t w = u8g2.getStrWidth(line)+4; u8g2.drawBox(MARGIN_X-2,26-(FONT_H-2),w,FONT_H); u8g2.setDrawColor(0); u8g2.drawStr(MARGIN_X,26,line); u8g2.setDrawColor(1);
  } else u8g2.drawStr(MARGIN_X,26,line);

  snprintf(line,sizeof(line),"End  : %6.1f", end);
  if (rangeSub==EditRangeSubState::ACTIVE && rangeFocus==1){
    uint8_t w = u8g2.getStrWidth(line)+4; u8g2.drawBox(MARGIN_X-2,38-(FONT_H-2),w,FONT_H); u8g2.setDrawColor(0); u8g2.drawStr(MARGIN_X,38,line); u8g2.setDrawColor(1);
  } else u8g2.drawStr(MARGIN_X,38,line);

  snprintf(line,sizeof(line),"Wrap : %s", wraps?"SI":"NO");
  if (rangeSub==EditRangeSubState::ACTIVE && rangeFocus==2){
    uint8_t w = u8g2.getStrWidth(line)+4; u8g2.drawBox(MARGIN_X-2,50-(FONT_H-2),w,FONT_H); u8g2.setDrawColor(0); u8g2.drawStr(MARGIN_X,50,line); u8g2.setDrawColor(1);
  } else u8g2.drawStr(MARGIN_X,50,line);

  if (rangeSub==EditRangeSubState::OPTIONS){
    const char* optOk = "OK"; const char* optEd = "Editar";
    uint8_t y = 62;
    uint8_t xOk = MARGIN_X;
    uint8_t okw = u8g2.getStrWidth(optOk) + 4;
    uint8_t xEd = xOk + okw + 10;
    uint8_t edw = u8g2.getStrWidth(optEd) + 4;
    if (rangeOptionsIndex == 0){ u8g2.drawBox(xOk-2,y-(FONT_H-2),okw,FONT_H); u8g2.setDrawColor(0); u8g2.drawStr(xOk,y,optOk); u8g2.setDrawColor(1);} else { u8g2.drawStr(xOk,y,optOk);}    
    if (rangeOptionsIndex == 1){ u8g2.drawBox(xEd-2,y-(FONT_H-2),edw,FONT_H); u8g2.setDrawColor(0); u8g2.drawStr(xEd,y,optEd); u8g2.setDrawColor(1);} else { u8g2.drawStr(xEd,y,optEd);}  
  } else {
    // Hint mínima sólo en ACTIVE
    u8g2.drawStr(MARGIN_X,62,"Click=avanza foco");
  }
  u8g2.sendBuffer();
}

static void drawConfirmHomeScreen() {
  u8g2.clearBuffer();
  u8g2.drawStr(MARGIN_X, 10, "No puede iniciar");
  u8g2.drawStr(MARGIN_X, 22, "sin hacer home.");
  u8g2.drawStr(MARGIN_X, 38, "Desea hacer home?");

  // Options: 0=No, 1=Si
  const char* no_str = "No";
  const char* si_str = "Si";
  uint8_t no_w = u8g2.getStrWidth(no_str);
  uint8_t si_w = u8g2.getStrWidth(si_str);
  uint8_t y = 58;
  uint8_t x_no = 32 - (no_w/2);
  uint8_t x_si = 96 - (si_w/2);

  if (confirmIndex == 0) { // "No" is selected
    u8g2.drawBox(x_no - 2, y - FONT_H + 2, no_w + 4, FONT_H);
    u8g2.setDrawColor(0);
    u8g2.drawStr(x_no, y, no_str);
    u8g2.setDrawColor(1);
    u8g2.drawStr(x_si, y, si_str);
  } else { // "Si" is selected
    u8g2.drawBox(x_si - 2, y - FONT_H + 2, si_w + 4, FONT_H);
    u8g2.setDrawColor(0);
    u8g2.drawStr(x_si, y, si_str);
    u8g2.setDrawColor(1);
    u8g2.drawStr(x_no, y, no_str);
  }
  u8g2.sendBuffer();
}

static void drawConfirmStopScreen() {
  u8g2.clearBuffer();
  u8g2.drawStr(MARGIN_X, 10, "Desea detener");
  u8g2.drawStr(MARGIN_X, 22, "el sistema?");

  // Options: 0=No, 1=Si
  const char* no_str = "No";
  const char* si_str = "Si";
  uint8_t no_w = u8g2.getStrWidth(no_str);
  uint8_t si_w = u8g2.getStrWidth(si_str);
  uint8_t y = 58;
  uint8_t x_no = 32 - (no_w/2);
  uint8_t x_si = 96 - (si_w/2);

  if (confirmIndex == 0) { // "No" is selected
    u8g2.drawBox(x_no - 2, y - FONT_H + 2, no_w + 4, FONT_H);
    u8g2.setDrawColor(0);
    u8g2.drawStr(x_no, y, no_str);
    u8g2.setDrawColor(1);
    u8g2.drawStr(x_si, y, si_str);
  } else { // "Si" is selected
    u8g2.drawBox(x_si - 2, y - FONT_H + 2, si_w + 4, FONT_H);
    u8g2.setDrawColor(0);
    u8g2.drawStr(x_si, y, si_str);
    u8g2.setDrawColor(1);
    u8g2.drawStr(x_no, y, no_str);
  }
  u8g2.sendBuffer();
}


// -------------------- MENU ACTIONS --------------------
// ===== NUEVO SISTEMA: navegación / edición =====
static const MenuNode* editingNode = nullptr; // nodo en edición
static bool editActive = false;               // para futuros modos avanzados
static bool actionMonitorActive = false;      // indica si mostramos pantalla acción
static unsigned long actionStartMs = 0;       // inicio de acción

// Dibujo pantalla acción / progreso
static void drawActionExec(){
  u8g2.clearBuffer();
  u8g2.drawStr(MARGIN_X,10,"ACCION");
  char line[64];
  unsigned long ms = millis() - actionStartMs;
  snprintf(line,sizeof(line),"State: %s", stateName(state)); u8g2.drawStr(MARGIN_X,24,line);
  snprintf(line,sizeof(line),"t= %lus", ms/1000UL); u8g2.drawStr(MARGIN_X,36,line);
  if (state == SysState::HOMING_SEEK){ u8g2.drawStr(MARGIN_X,48,"Homing..."); }
  else if (state == SysState::ROTATING){ u8g2.drawStr(MARGIN_X,48,"Rotando..."); }
  else if (state == SysState::RUNNING){ u8g2.drawStr(MARGIN_X,48,"Running..."); }
  else if (state == SysState::STOPPING){ u8g2.drawStr(MARGIN_X,48,"Parando..."); }
  u8g2.drawStr(MARGIN_X,62,"Click=STOP / Back");
  u8g2.sendBuffer();
}

static void drawFaultScreen(){
  u8g2.clearBuffer();
  u8g2.drawStr(MARGIN_X,12,"*** FAULT ***");
  u8g2.drawStr(MARGIN_X,28,"Revise log serial");
  u8g2.drawStr(MARGIN_X,44,"Click=Menu");
  u8g2.sendBuffer();
}

// ========= WiFi UI (Fase 1B flujo amigable) =========
#include "ui_menu_model.h"
using WifiState = WifiMgr::State;
static uint8_t wifiListIndex = 0; // índice seleccionado en lista
static uint8_t wifiListOffset = 0; // para scroll
static const uint8_t WIFI_LIST_MAX_VISIBLE = 5; // 5 líneas de redes + header

// Password editor (simple versión inicial)
static char wifiPassword[33] = {0};
static uint8_t wifiPwLen = 0; // longitud actual
static uint8_t wifiPwCursor = 0; // posición de edición
static bool wifiPwEditing = true; // true: editando caracteres, false: foco en botones
static uint8_t wifiPwButtonIndex = 0; // 0=OK 1=Clr 2=Editar
static uint32_t lastClickMs = 0; // para doble click
static const uint32_t DOUBLE_CLICK_WINDOW_MS = 350;
static const char* PW_CHARSET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.:@#!?*$%&+/="" ";
static int pwCharsetIndex = 0; // índice del char actual
static uint32_t pwPressStartMs = 0; // para long press
static const uint32_t PW_LONG_PRESS_MS = 600;
static bool pwPressArmed = false; // requiere flanco para armar long press
static uint32_t pwModeEnterMs = 0; // evita long press inmediato al entrar
// Long press deshabilitado para evitar salto involuntario

static char currentPwChar(){
  size_t n = strlen(PW_CHARSET);
  if (n==0) return '?';
  if (pwCharsetIndex < 0) pwCharsetIndex = 0;
  if (pwCharsetIndex >= (int)n) pwCharsetIndex = (int)n-1;
  return PW_CHARSET[pwCharsetIndex];
}

static void drawWifiPwEdit(){
  u8g2.clearBuffer();
  u8g2.drawStr(MARGIN_X,10,"Password");
  // Mostrar password en claro
  char buf[40];
  strncpy(buf, wifiPassword, sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
  u8g2.drawStr(MARGIN_X,24, buf);
  // Cursor indicador
  int cx = MARGIN_X + u8g2.getStrWidth(buf);
  if (wifiPwEditing){
    char c[2]={currentPwChar(),0};
    u8g2.drawStr(cx,24,c);
    u8g2.drawBox(cx-1,26, u8g2.getStrWidth(c)+2,1); // subrayado
  }
  // Botones
  int yBtns = 50;
  const char* okLbl = "[OK]";
  const char* clrLbl = "[Clr]";
  const char* edLbl = "[Editar]";
  int okW = u8g2.getStrWidth(okLbl);
  int clrW = u8g2.getStrWidth(clrLbl);
  int edW = u8g2.getStrWidth(edLbl);
  int totalW = okW + 4 + clrW + 4 + edW;
  int startX = (128 - totalW)/2;
  int xOk = startX;
  int xClr = xOk + okW + 4;
  int xEd = xClr + clrW + 4;
  // OK
  if (!wifiPwEditing && wifiPwButtonIndex==0){ u8g2.drawBox(xOk-2,yBtns-10, okW+4,11); u8g2.setDrawColor(0); }
  u8g2.drawStr(xOk,yBtns, okLbl);
  if (!wifiPwEditing && wifiPwButtonIndex==0){ u8g2.setDrawColor(1); }
  // Clr
  if (!wifiPwEditing && wifiPwButtonIndex==1){ u8g2.drawBox(xClr-2,yBtns-10, clrW+4,11); u8g2.setDrawColor(0); }
  u8g2.drawStr(xClr,yBtns, clrLbl);
  if (!wifiPwEditing && wifiPwButtonIndex==1){ u8g2.setDrawColor(1); }
  // Editar
  if (!wifiPwEditing && wifiPwButtonIndex==2){ u8g2.drawBox(xEd-2,yBtns-10, edW+4,11); u8g2.setDrawColor(0); }
  u8g2.drawStr(xEd,yBtns, edLbl);
  if (!wifiPwEditing && wifiPwButtonIndex==2){ u8g2.setDrawColor(1); }
  u8g2.drawStr(MARGIN_X,62,"Click=Avanza Dbl=Back Min8");
  u8g2.sendBuffer();
}

static void drawWifiScanning(){
  u8g2.clearBuffer();
  u8g2.drawStr(MARGIN_X,10,"WiFi Scan");
  // animación simple puntos
  uint8_t dots = (millis()/400)%4; char buf[16];
  snprintf(buf,sizeof(buf),"Scanning%.*s", dots, "....");
  u8g2.drawStr(MARGIN_X,28, buf);
  u8g2.drawStr(MARGIN_X,54,"Back=Click");
  u8g2.sendBuffer();
}

static void drawWifiList(){
  u8g2.clearBuffer();
  int total = WifiMgr::networkCount();
  char hdr[32]; snprintf(hdr,sizeof(hdr),"Redes: %d", total);
  u8g2.drawStr(MARGIN_X,10,hdr);
  // ajustar offset si selección sale de ventana
  if (wifiListIndex < wifiListOffset) wifiListOffset = wifiListIndex;
  if (wifiListIndex >= wifiListOffset + WIFI_LIST_MAX_VISIBLE) wifiListOffset = wifiListIndex - WIFI_LIST_MAX_VISIBLE + 1;
  for (uint8_t i=0;i<WIFI_LIST_MAX_VISIBLE;i++){
    int idx = wifiListOffset + i;
    if (idx >= total) break;
    const char* ssid = WifiMgr::ssidAt(idx);
    char line[20];
    // recortar si largo>18
    strncpy(line, ssid?ssid:"", 18); line[18]='\0';
    int y = 24 + i*10; // starting y
    if (idx == wifiListIndex){
      u8g2.drawBox(0,y-8,128,10);
      u8g2.setDrawColor(0);
      u8g2.drawStr(MARGIN_X,y,line);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(MARGIN_X,y,line);
    }
  }
  u8g2.drawStr(90,62,"OK=Click");
  u8g2.sendBuffer();
}

static void drawWifiConnecting(){
  u8g2.clearBuffer();
  u8g2.drawStr(MARGIN_X,10,"Conectando");
  const char* ssid = WifiMgr::ssidAt(WifiMgr::selectedIndex());
  if (ssid){ u8g2.drawStr(MARGIN_X,24, ssid); }
  uint8_t dots = (millis()/400)%4; char buf[16]; snprintf(buf,sizeof(buf),"...%.*s",dots,"....");
  u8g2.drawStr(MARGIN_X,38, buf);
  u8g2.drawStr(MARGIN_X,54,"Back=Click");
  u8g2.sendBuffer();
}

static void drawWifiResult(){
  u8g2.clearBuffer();
  if (WifiMgr::state() == WifiState::CONNECTED){
    u8g2.drawStr(MARGIN_X,12,"WiFi OK");
    u8g2.drawStr(MARGIN_X,28, WifiMgr::ipStr());
  } else {
    u8g2.drawStr(MARGIN_X,12,"WiFi FAIL");
    WifiMgr::FailReason fr = WifiMgr::failReason();
    const char* msg = "";
    switch (fr){
      case WifiMgr::FailReason::SCAN_TIMEOUT: msg="Scan timeout"; break;
      case WifiMgr::FailReason::SCAN_FAILED: msg="Scan failed"; break;
      case WifiMgr::FailReason::CONNECT_TIMEOUT: msg="Conn timeout"; break;
      case WifiMgr::FailReason::INVALID_PASSWORD: msg="Bad password"; break;
      case WifiMgr::FailReason::NO_SSID: msg="SSID no disp"; break;
      default: msg="Error"; break;
    }
    u8g2.drawStr(MARGIN_X,28,msg);
  }
  u8g2.drawStr(MARGIN_X,54,"Click=Volver");
  u8g2.sendBuffer();
}

void initNewMenuModel(){
  uiMenuModelInit();
  uiMode = UiViewMode::MAIN_MENU;
}

static void enterSubmenu(const MenuNode& N){
  if (uiNav.stackCount < 6){
    // Log específico al entrar al submenu Internet
    if (N.label && strcmp(N.label, "8. Internet")==0){
      logPrint("WIFI","Entrando submenu Internet");
    }
    uiNav.stack[uiNav.stackCount++] = &N;
    uiNav.currentList = N.children;
    uiNav.currentCount = N.childCount;
    uiNav.index = 0;
  }
}

static void goBack(){
  if (uiNav.stackCount==0){ uiMode = UiViewMode::MAIN_MENU; return; }
  // Detectar si estamos saliendo del submenu Internet antes de hacer pop
  if (uiNav.stackCount>0){
    const MenuNode* leaving = uiNav.stack[uiNav.stackCount-1];
    if (leaving && leaving->label && strcmp(leaving->label, "8. Internet")==0){
      logPrint("WIFI","Saliendo submenu Internet");
    }
  }
  uiNav.stackCount--;
  if (uiNav.stackCount==0){
    uiNav.currentList = MAIN_MENU;
    uiNav.currentCount = MAIN_MENU_COUNT;
    uiNav.index = 0;
  } else {
    const MenuNode* parent = uiNav.stack[uiNav.stackCount-1];
    uiNav.currentList = parent->children;
    uiNav.currentCount = parent->childCount;
  }
}

static void startEdit(const MenuNode& N){
  editingNode = &N;
  uiMode = UiViewMode::EDIT_VALUE;
  editActive = true;
  encVelLastMs = 0; encVelEma = 0.0f;
  editSub = EditValueSubState::ACTIVE;
  optionsIndex = 0;
  editValueChanged = false;
  Buzzer::beepNav(); // beep ingreso modo edición valor (garantizado incluso si llamada cambia en otra parte)
  // Snapshot inicial por tipo
  switch (N.type){
    case MenuNodeType::VALUE_FLOAT: editSnapshotFloat = *N.data.vf.ptr; break;
    case MenuNodeType::VALUE_INT:   editSnapshotInt   = *N.data.vi.ptr; break;
    case MenuNodeType::VALUE_ENUM:  editSnapshotEnum  = *N.data.ve.ptr; break;
    default: break;
  }
}

static void startRangeEdit(const MenuNode& N){
  rangeEditingNode = &N;
  rangeFocus = 0;
  uiMode = UiViewMode::EDIT_RANGE;
  encVelLastMs = 0; encVelEma = 0.0f;
  rangeSub = EditRangeSubState::ACTIVE;
  rangeOptionsIndex = 0;
  // snapshot
  rangeSnapStart = *N.data.rd.startPtr;
  rangeSnapEnd   = *N.data.rd.endPtr;
  rangeSnapWrap  = *N.data.rd.wrapsPtr;
  rangeChanged   = false;
  Buzzer::beepNav(); // beep ingreso modo edición rango
}

static void applyDeltaToNode(const MenuNode& N, int8_t encDelta){
  if (encDelta==0) return;
  switch (N.type){
    case MenuNodeType::VALUE_FLOAT: {
      float v = *N.data.vf.ptr;
      float step = N.data.vf.step * encDelta;
      v += step;
      if (v < N.data.vf.minV) v = N.data.vf.minV;
      if (v > N.data.vf.maxV) v = N.data.vf.maxV;
      *N.data.vf.ptr = v;
      applyConfigToProfiles();
    } break;
    case MenuNodeType::VALUE_INT: {
      int32_t v = *N.data.vi.ptr;
      int32_t step = N.data.vi.step * encDelta;
      v += step;
      if (v < N.data.vi.minV) v = N.data.vi.minV;
      if (v > N.data.vi.maxV) v = N.data.vi.maxV;
      *N.data.vi.ptr = v;
      applyConfigToProfiles();
    } break;
    case MenuNodeType::VALUE_ENUM: {
      int v = *N.data.ve.ptr + encDelta;
      if (v < 0) v = N.data.ve.count -1;
      if (v >= N.data.ve.count) v = 0;
      *N.data.ve.ptr = (uint8_t)v;
      // aplicar efectos secundarios (ej: master dir / s-curve)
      if (editingNode == &N){
        // sincronizar variables reales
        // Hicimos rename de etiquetas ("1. Master Dir", "2. S Curve") así que usamos substring case-insensitive
        auto icontains = [](const char* hay, const char* needle){
          if (!hay || !needle) return false;
          size_t nlen = strlen(needle);
          for (const char* p = hay; *p; ++p){
            size_t i=0; while (i<nlen && p[i] && tolower((unsigned char)p[i])==tolower((unsigned char)needle[i])) ++i;
            if (i==nlen) return true;
          }
          return false;
        };
        if (icontains(N.label, "master dir")){
          master_direction = (*N.data.ve.ptr == 0); // 0=CW
          inverse_direction = !master_direction;
          Cfg.master_dir_cw = master_direction; // reflejar para persistencia
          logPrintf("CONFIG","Master Dir=%s", master_direction?"CW":"CCW");
        } else if (icontains(N.label, "s curve")){
          Cfg.enable_s_curve = (*N.data.ve.ptr == 1);
          logPrintf("CONFIG","S-Curve %s", Cfg.enable_s_curve?"ON":"OFF");
        }
        applyConfigToProfiles();
      }
    } break;
    default: break;
  }
}

// -------------------- UI FLOW --------------------
void uiProcess(int8_t encDelta, bool encClick) {
  // Transición inicial al nuevo menú: si estamos en STATUS y click => mostrar menú raíz
  if (uiScreen == UiScreen::STATUS && encClick) {
    uiMode = UiViewMode::MAIN_MENU;
    initNewMenuModel();
    uiScreen = UiScreen::MENU; // reflejar entrada a menú
    Buzzer::beepNav();
    // IMPORTANTE: retornamos para NO consumir este mismo click como "entrar al primer submenu"
    // así el usuario ve primero el menú raíz completo.
    return;
    uint32_t nowBeep = millis();
  }

  switch (uiMode) {
    case UiViewMode::MAIN_MENU:
    case UiViewMode::SUB_MENU: {
      if (encDelta != 0) {
        int ni = (int)uiNav.index + encDelta;
        if (ni < 0) ni = 0;
        if (ni >= uiNav.currentCount) ni = uiNav.currentCount -1;
        if (ni != uiNav.index){
          uiNav.index = (uint8_t)ni;
          uint32_t now = millis();
            if (now - lastScrollBeepMs >= 70){
              Buzzer::beepNav();
              lastScrollBeepMs = now;
            }
        }
      }
      if (encClick) {
        const MenuNode& N = uiNav.currentList[uiNav.index];
        if (N.type == MenuNodeType::SUBMENU) {
          enterSubmenu(N);
          uiMode = UiViewMode::SUB_MENU;
          Buzzer::beepNav();
        } else if (N.type == MenuNodeType::PLACEHOLDER) {
          // Root exit: si stackCount==0 y etiqueta empieza con '<' salimos a STATUS
          if (uiNav.stackCount==0 && N.label[0]=='<') {
            uiScreen = UiScreen::STATUS;
            uiMode = UiViewMode::MAIN_MENU; // modo listo para re-entrar
            Buzzer::beepBack();
            return;
          } else {
            goBack();
            Buzzer::beepBack();
          }
        } else if (N.type == MenuNodeType::ACTION) {
          if (nodeDisabled(N)) {
            // ignorar
          } else if (N.data.act.exec) {
            N.data.act.exec();
            Buzzer::beepNav();
          }
          // activar monitor si cambia a un estado dinámico
          if (state == SysState::HOMING_SEEK || state == SysState::ROTATING || state == SysState::RUNNING){
            actionMonitorActive = true;
            actionStartMs = millis();
            uiMode = UiViewMode::ACTION_EXEC;
          }
        } else if (N.type == MenuNodeType::VALUE_FLOAT || N.type == MenuNodeType::VALUE_INT || N.type == MenuNodeType::VALUE_ENUM) {
          startEdit(N);
          Buzzer::beepNav(); // beep al entrar a edición de valor
        } else if (N.type == MenuNodeType::RANGE_DEG) {
          startRangeEdit(N);
          Buzzer::beepNav(); // beep al entrar a edición de rango
        }
      }
    } break;

    case UiViewMode::WIFI_SCANNING: {
      // Encoder no hace nada; click = cancelar/volver
      if (encClick){
        // cancelar si en progreso
        if (WifiMgr::state() == WifiState::SCANNING){ /* no API cancelar scan async fácil; dejamos FAIL visual */ }
        uiMode = UiViewMode::SUB_MENU; // regresar al menú previo
      }
      // transición automática a lista cuando SCAN_DONE
      if (WifiMgr::state() == WifiState::SCAN_DONE){
        wifiListIndex = 0; wifiListOffset = 0;
        uiMode = UiViewMode::WIFI_LIST;
      } else if (WifiMgr::state() == WifiState::FAIL){
        uiMode = UiViewMode::WIFI_RESULT;
      }
    } break;

    case UiViewMode::WIFI_LIST: {
      int total = WifiMgr::networkCount(); if (total < 0) total = 0;
      if (encDelta != 0 && total>0){
        int ni = (int)wifiListIndex + (encDelta>0?1:-1);
        if (ni < 0) ni = 0; if (ni >= total) ni = total-1;
        if (ni != wifiListIndex){ wifiListIndex = (uint8_t)ni; Buzzer::beepNav(); }
      }
      if (encClick){
        if (total==0){ uiMode = UiViewMode::WIFI_RESULT; }
        else {
          WifiMgr::selectNetwork(wifiListIndex);
          // Ir al editor de password en lugar de conectar directo
          uiMode = UiViewMode::WIFI_PW_EDIT;
          // Armar contexto de password edit
          wifiPwEditing = true; wifiPwButtonIndex = 0; pwPressStartMs = 0; pwPressArmed=false; pwModeEnterMs = millis();
          wifiPwEditing = true; wifiPwButtonIndex = 0; // mantener foco en edición
          Buzzer::beepNav();
        }
      }
    } break;

    case UiViewMode::WIFI_CONNECTING: {
      if (encClick){
        // permitir abortar → mostrar resultado FAIL
        if (WifiMgr::state() == WifiState::CONNECTING){ WifiMgr::cancelConnect(); }
        uiMode = UiViewMode::WIFI_RESULT;
      }
      if (WifiMgr::state() == WifiState::CONNECTED){ uiMode = UiViewMode::WIFI_RESULT; }
      else if (WifiMgr::state() == WifiState::FAIL){ uiMode = UiViewMode::WIFI_RESULT; }
    } break;

    case UiViewMode::WIFI_RESULT: {
      if (encClick){
        // Tras resultado fallido relanzar scan automáticamente para comodidad
        if (WifiMgr::state() == WifiState::FAIL){
          WifiMgr::beginScan();
          uiMode = UiViewMode::WIFI_SCANNING;
        } else {
          uiMode = UiViewMode::SUB_MENU; // éxito => volver
        }
      }
    } break;

    case UiViewMode::WIFI_PW_EDIT: {
      // Encoder: si editando caracteres, giro arriba/abajo cambia char; giro izquierda/derecha mueve cursor? (usamos char cycler solamente)
      if (wifiPwEditing){
        if (encDelta != 0){
          int n = (int)strlen(PW_CHARSET);
          pwCharsetIndex += (encDelta>0?1:-1);
          if (pwCharsetIndex < 0) pwCharsetIndex = n-1;
          if (pwCharsetIndex >= n) pwCharsetIndex = 0;
          Buzzer::beepNav();
        }
        // Long press detection
        bool rawPressed = App::encoderButtonPressedRaw();
        uint32_t nowLP = millis();
        // Ignorar long press durante primeros 250ms tras entrar al modo
        if (nowLP - pwModeEnterMs >= 250) {
          if (rawPressed) {
            if (!pwPressArmed) { pwPressArmed = true; pwPressStartMs = nowLP; }
            else if (pwPressStartMs && nowLP - pwPressStartMs >= PW_LONG_PRESS_MS) {
              wifiPwEditing = false; wifiPwButtonIndex = 0; pwPressStartMs = 0; pwPressArmed=false; Buzzer::beepNav();
            }
          } else {
            // liberar
            pwPressArmed = false; pwPressStartMs = 0;
  // Long press removido
  WifiMgr::State ws = WifiMgr::state();
  if (ws == WifiMgr::State::CONNECTED){
    const char* ssidTxt = WifiMgr::ssidAt(WifiMgr::selectedIndex());
    if (!ssidTxt || !*ssidTxt){ ssidTxt = Cfg.wifi_ssid[0]? Cfg.wifi_ssid : "(?)"; }
    char head[48]; snprintf(head,sizeof(head), "Wifi - %s", ssidTxt);
    u8g2.drawStr(MARGIN_X,10, head);
  } else if (ws == WifiMgr::State::CONNECTING){
    u8g2.drawStr(MARGIN_X,10, "Wifi - Conectando");
  } else {
    u8g2.drawStr(MARGIN_X,10, "Sin conexion");
  }
          }
        }
        if (encClick){
          uint32_t now = millis();
          bool dbl = (now - lastClickMs) <= DOUBLE_CLICK_WINDOW_MS;
          lastClickMs = now;
          if (dbl){
            // backspace
            if (wifiPwLen > 0){
              wifiPwLen--; wifiPassword[wifiPwLen]='\0';
              Buzzer::beepBack();
            }
          } else {
            // aceptar char actual si hay espacio
            if (wifiPwLen < 32){
              wifiPassword[wifiPwLen] = currentPwChar();
              wifiPwLen++; wifiPassword[wifiPwLen]='\0';
              Buzzer::beepNav();
            }
          }
        }
        // Long press -> cambiar a botones (simplificado: si password tiene al menos 1 char y encoder delta==0 por 600ms sin cambios? omitimos por ahora y dejamos transición vía rotación cuando lleno)
      } else { // foco en botones
        if (encDelta != 0){
          int countBtns = 3;
          int ni = (int)wifiPwButtonIndex + (encDelta>0?1:-1);
          if (ni < 0) ni = countBtns-1; if (ni >= countBtns) ni = 0;
          if (ni != (int)wifiPwButtonIndex){ wifiPwButtonIndex = (uint8_t)ni; Buzzer::beepNav(); }
        }
        if (encClick){
          if (wifiPwButtonIndex == 0){
            // OK -> iniciar conexión
            if (wifiPwLen < 8){
              // mínima longitud
              Buzzer::beepError();
            } else {
              WifiMgr::startConnect(wifiPassword);
              uiMode = UiViewMode::WIFI_CONNECTING;
              // Confirmación con beepSave (acción importante)
              Buzzer::beepSave();
            }
          } else if (wifiPwButtonIndex == 1){
            // Clr
            if (wifiPwLen > 0){ wifiPwLen = 0; wifiPassword[0]='\0'; Buzzer::beepBack(); }
          } else if (wifiPwButtonIndex == 2){
            // Editar
            wifiPwEditing = true; Buzzer::beepNav();
          } 
        }
      }
      // Transición a modo botones si password llena y giro largo no usado: si len==32 y se hace click normal entra a botones
      if (wifiPwEditing && wifiPwLen >= 32){ wifiPwEditing = false; wifiPwButtonIndex = 0; }
    } break;

    case UiViewMode::EDIT_VALUE: {
      if (!editingNode){ uiMode = UiViewMode::SUB_MENU; break; }
      if (editSub == EditValueSubState::ACTIVE){
        if (encDelta != 0){ applyDeltaToNode(*editingNode, encDelta); }
        if (encClick){
          // Detectar cambio
          switch (editingNode->type){
            case MenuNodeType::VALUE_FLOAT: editValueChanged = (*editingNode->data.vf.ptr != editSnapshotFloat); break;
            case MenuNodeType::VALUE_INT:   editValueChanged = (*editingNode->data.vi.ptr != editSnapshotInt); break;
            case MenuNodeType::VALUE_ENUM:  editValueChanged = (*editingNode->data.ve.ptr != editSnapshotEnum); break;
            default: editValueChanged = false; break;
          }
          if (editValueChanged){
            saveConfig();
            logPrint("CONFIG","Guardado EEPROM (valor)");
            Buzzer::beepSave();
            editValueChanged = false; // ya guardado
          }
            editSub = EditValueSubState::OPTIONS;
            optionsIndex = 0;
        }
      } else { // OPTIONS
        if (encDelta != 0){
          int ni = (int)optionsIndex + (encDelta>0?1:-1);
          if (ni < 0) ni = 1; if (ni > 1) ni = 0;
          if ((uint8_t)ni != optionsIndex){
            optionsIndex = (uint8_t)ni;
            Buzzer::beepNav(); // beep al mover selección OK/Editar
          }
        }
        if (encClick){
          if (optionsIndex == 0){ // OK salir
            editingNode = nullptr;
            uiMode = UiViewMode::SUB_MENU;
            Buzzer::beepNav(); // beep al salir de edición valor
          } else if (optionsIndex == 1){ // Editar de nuevo
            // nuevo snapshot
            switch (editingNode->type){
              case MenuNodeType::VALUE_FLOAT: editSnapshotFloat = *editingNode->data.vf.ptr; break;
              case MenuNodeType::VALUE_INT:   editSnapshotInt   = *editingNode->data.vi.ptr; break;
              case MenuNodeType::VALUE_ENUM:  editSnapshotEnum  = *editingNode->data.ve.ptr; break;
              default: break;
            }
            editSub = EditValueSubState::ACTIVE;
            Buzzer::beepNav(); // beep al volver a modo edición
          }
        }
      }
    } break;

    case UiViewMode::EDIT_RANGE: {
      if (!rangeEditingNode){ uiMode = UiViewMode::SUB_MENU; break; }
      const MenuNode& N = *rangeEditingNode;
      if (rangeSub == EditRangeSubState::ACTIVE){
        if (encDelta != 0){
          bool changed = false;
          if (rangeFocus == 0){ // start
            float v = *N.data.rd.startPtr + (float)encDelta * 1.0f; // paso 1°
            if (v < -360.0f) v += 360.0f; if (v > 360.0f) v -= 360.0f;
            *N.data.rd.startPtr = v; changed = true;
          } else if (rangeFocus == 1){ // end
            float v = *N.data.rd.endPtr + (float)encDelta * 1.0f;
            if (v < -360.0f) v += 360.0f; if (v > 360.0f) v -= 360.0f;
            *N.data.rd.endPtr = v; changed = true;
          } else if (rangeFocus == 2){ // wrap toggle con giro
            if (encDelta != 0){ *N.data.rd.wrapsPtr = !*N.data.rd.wrapsPtr; changed = true; }
          }
            applyConfigToProfiles();
            if (changed){
              uint32_t nowR = millis();
              if (nowR - lastEditRangeBeepMs >= EDIT_BEEP_MIN_INTERVAL_MS){
                Buzzer::beepNav();
                lastEditRangeBeepMs = nowR;
              }
            }
        }
        if (encClick){
          rangeFocus++;
          if (rangeFocus > 2){
            // Pasar a OPTIONS: evaluar si cambió
            rangeChanged = (
              (*N.data.rd.startPtr != rangeSnapStart) ||
              (*N.data.rd.endPtr   != rangeSnapEnd)   ||
              (*N.data.rd.wrapsPtr != rangeSnapWrap)
            );
            if (rangeChanged){
              saveConfig();
              logPrint("CONFIG","Guardado EEPROM (rango)");
              Buzzer::beepSave();
              rangeChanged = false;
            }
            rangeSub = EditRangeSubState::OPTIONS;
            rangeOptionsIndex = 0;
          }
        }
      } else { // OPTIONS
        if (encDelta != 0){
          int ni = (int)rangeOptionsIndex + (encDelta>0?1:-1);
          if (ni < 0) ni = 1; if (ni > 1) ni = 0;
          if ((uint8_t)ni != rangeOptionsIndex){
            rangeOptionsIndex = (uint8_t)ni;
            Buzzer::beepNav(); // beep al mover selección en opciones rango
          }
        }
        if (encClick){
          if (rangeOptionsIndex == 0){ // OK salir
            rangeEditingNode = nullptr;
            uiMode = UiViewMode::SUB_MENU;
            Buzzer::beepNav(); // beep al salir rango
          } else if (rangeOptionsIndex == 1){ // Editar nuevamente
            // refrescar snapshots
            rangeSnapStart = *N.data.rd.startPtr;
            rangeSnapEnd   = *N.data.rd.endPtr;
            rangeSnapWrap  = *N.data.rd.wrapsPtr;
            rangeFocus = 0;
            rangeSub = EditRangeSubState::ACTIVE;
            Buzzer::beepNav(); // beep al volver a editar rango
          }
        }
      }
    } break;

    case UiViewMode::ACTION_EXEC: {
      // Si entra FAULT → pantalla fault
      if (state == SysState::FAULT){
        uiMode = UiViewMode::FAULT_SCREEN;
        break;
      }
      // Auto-salida si estado ya no es dinámico
      if (!(state == SysState::HOMING_SEEK || state == SysState::ROTATING || state == SysState::RUNNING || state == SysState::STOPPING)){
        uiMode = UiViewMode::SUB_MENU;
      } else {
        if (encClick){
          // Click => STOP si aplica
            if (state == SysState::RUNNING || state == SysState::ROTATING){ state = SysState::STOPPING; }
            else if (state == SysState::HOMING_SEEK){ /* no forzamos stop del homing de momento */ }
            else if (state == SysState::STOPPING){ /* nada */ }
        }
      }
    } break;

    case UiViewMode::FAULT_SCREEN: {
      if (encClick){
        // Volver a menú principal (el sistema deberá ser recuperado externamente)
        uiMode = UiViewMode::MAIN_MENU;
        uiNav.currentList = MAIN_MENU;
        uiNav.currentCount = MAIN_MENU_COUNT;
        uiNav.index = 0;
      }
    } break;

    default: break; // otros modos aún no implementados
  }
}

void uiRender() {
  // Splash tiene prioridad absoluta al inicio
  if (splashActive){
    uint32_t now = millis();
    switch (splashPhase){
      case SplashPhase::TYPE_TITLE: {
        if (now - lastTypeMs >= TYPE_INTERVAL_MS){
          lastTypeMs = now;
          size_t titleLen = strlen(SPLASH_TITLE);
          if (typedChars < titleLen){ typedChars++; }
          if (typedChars >= titleLen){
            splashPhase = SplashPhase::PAUSE_BETWEEN;
            pauseStartMs = now;
            typedChars = 0; // preparar segunda línea
          }
        }
      } break;
      case SplashPhase::PAUSE_BETWEEN: {
        if (now - pauseStartMs >= PAUSE_BETWEEN_MS){
          splashPhase = SplashPhase::TYPE_SUB;
          secondLineStarted = true;
          lastTypeMs = now;
        }
      } break;
      case SplashPhase::TYPE_SUB: {
        if (now - lastTypeMs >= TYPE_INTERVAL_MS){
          lastTypeMs = now;
          if (typedChars < strlen(SPLASH_SUB_FULL)) typedChars++; else {
            splashPhase = SplashPhase::MELODY_HOLD;
            holdStartMs = now;
            Buzzer::startStartupMelody();
          }
        }
      } break;
      case SplashPhase::MELODY_HOLD: {
        if (now - holdStartMs >= HOLD_MS){
          splashPhase = SplashPhase::DONE;
          splashActive = false;
          u8g2.setFont(u8g2_font_6x10_tf);
        }
      } break;
      case SplashPhase::DONE: break;
    }
    drawSplash();
    if (splashActive) return;
  }

  // Render WiFi pantallas dedicadas
  if (uiMode == UiViewMode::WIFI_SCANNING){ drawWifiScanning(); return; }
  if (uiMode == UiViewMode::WIFI_LIST){ drawWifiList(); return; }
  if (uiMode == UiViewMode::WIFI_CONNECTING){ drawWifiConnecting(); return; }
  if (uiMode == UiViewMode::WIFI_RESULT){ drawWifiResult(); return; }
  if (uiMode == UiViewMode::WIFI_PW_EDIT){ drawWifiPwEdit(); return; }
  if (uiScreen == UiScreen::STATUS && uiMode == UiViewMode::MAIN_MENU) {
    // Aún no se ha abierto la lista; mostrar status mientras tanto
    drawStatusScreen();
    return;
  }
  switch (uiMode) {
    case UiViewMode::MAIN_MENU:
    case UiViewMode::SUB_MENU:
      drawHierMenuList();
      break;
    case UiViewMode::EDIT_VALUE:
      if (editingNode) drawValueEdit(*editingNode);
      break;
    case UiViewMode::EDIT_RANGE:
      if (rangeEditingNode) drawRangeEdit(*rangeEditingNode);
      break;
    case UiViewMode::ACTION_EXEC:
      drawActionExec();
      break;
    case UiViewMode::FAULT_SCREEN:
      drawFaultScreen();
      break;
    default:
      drawStatusScreen();
      break;
  }
}

} // namespace App
