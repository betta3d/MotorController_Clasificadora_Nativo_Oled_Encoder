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

namespace App {

// -------------------- OLED INSTANCE --------------------
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ SCL, /* data=*/ SDA
);

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

// -------------------- DRAWING --------------------
void oledInit() {
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
}

static void drawStatusScreen() {
  float ang = currentAngleDeg();
  float steps_per_cm = (Cfg.cm_per_rev > 0.0f) ? ((float)stepsPerRev / Cfg.cm_per_rev) : 0.0f;
  float v_cmps = (steps_per_cm > 0.0f) ? (v / steps_per_cm) : 0.0f;
  const char* sect = sectorName(ang);

  u8g2.clearBuffer();
  u8g2.drawStr(MARGIN_X, 10, "Control Movimientos");

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

void initNewMenuModel(){
  uiMenuModelInit();
  uiMode = UiViewMode::MAIN_MENU;
}

static void enterSubmenu(const MenuNode& N){
  if (uiNav.stackCount < 6){
    uiNav.stack[uiNav.stackCount++] = &N;
    uiNav.currentList = N.children;
    uiNav.currentCount = N.childCount;
    uiNav.index = 0;
  }
}

static void goBack(){
  if (uiNav.stackCount==0){ uiMode = UiViewMode::MAIN_MENU; return; }
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
            return;
          } else {
            goBack();
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
          Buzzer::beepNav();
        } else if (N.type == MenuNodeType::RANGE_DEG) {
          startRangeEdit(N);
          Buzzer::beepNav();
        }
      }
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
          optionsIndex = (uint8_t)ni;
        }
        if (encClick){
          if (optionsIndex == 0){ // OK salir
            editingNode = nullptr;
            uiMode = UiViewMode::SUB_MENU;
          } else if (optionsIndex == 1){ // Editar de nuevo
            // nuevo snapshot
            switch (editingNode->type){
              case MenuNodeType::VALUE_FLOAT: editSnapshotFloat = *editingNode->data.vf.ptr; break;
              case MenuNodeType::VALUE_INT:   editSnapshotInt   = *editingNode->data.vi.ptr; break;
              case MenuNodeType::VALUE_ENUM:  editSnapshotEnum  = *editingNode->data.ve.ptr; break;
              default: break;
            }
            editSub = EditValueSubState::ACTIVE;
          }
        }
      }
    } break;

    case UiViewMode::EDIT_RANGE: {
      if (!rangeEditingNode){ uiMode = UiViewMode::SUB_MENU; break; }
      const MenuNode& N = *rangeEditingNode;
      if (rangeSub == EditRangeSubState::ACTIVE){
        if (encDelta != 0){
          if (rangeFocus == 0){ // start
            float v = *N.data.rd.startPtr + (float)encDelta * 1.0f; // paso 1°
            if (v < -360.0f) v += 360.0f; if (v > 360.0f) v -= 360.0f;
            *N.data.rd.startPtr = v;
          } else if (rangeFocus == 1){ // end
            float v = *N.data.rd.endPtr + (float)encDelta * 1.0f;
            if (v < -360.0f) v += 360.0f; if (v > 360.0f) v -= 360.0f;
            *N.data.rd.endPtr = v;
          } else if (rangeFocus == 2){ // wrap toggle con giro
            if (encDelta != 0){ *N.data.rd.wrapsPtr = !*N.data.rd.wrapsPtr; }
          }
          applyConfigToProfiles();
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
          rangeOptionsIndex = (uint8_t)ni;
        }
        if (encClick){
          if (rangeOptionsIndex == 0){ // OK salir
            rangeEditingNode = nullptr;
            uiMode = UiViewMode::SUB_MENU;
          } else if (rangeOptionsIndex == 1){ // Editar nuevamente
            // refrescar snapshots
            rangeSnapStart = *N.data.rd.startPtr;
            rangeSnapEnd   = *N.data.rd.endPtr;
            rangeSnapWrap  = *N.data.rd.wrapsPtr;
            rangeFocus = 0;
            rangeSub = EditRangeSubState::ACTIVE;
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
