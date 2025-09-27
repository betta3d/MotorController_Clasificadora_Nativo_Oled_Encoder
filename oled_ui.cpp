#include "oled_ui.h"
#include "globals.h"
#include "motion.h"
#include "eeprom_store.h"
#include "logger.h"
#include "homing.h"
#include "ui_menu_model.h"

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

static void drawHierMenuList() {
  u8g2.clearBuffer();
  if (uiNav.stackCount==0){
    u8g2.drawStr(MARGIN_X, 10, "MENU");
  } else {
    const char* raw = uiNav.stack[uiNav.stackCount-1]->label;
    // Copiar y convertir a MAYÚSCULAS solo para título
    char up[20];
    size_t i=0; for (; i < sizeof(up)-1 && raw[i]; ++i){ char c=raw[i]; if (c>='a'&&c<='z') c = c - 'a' + 'A'; up[i]=c; }
    up[i]='\0';
    u8g2.drawStr(MARGIN_X,10, up);
  }
  int total = uiNav.currentCount;
  int first = (int)uiNav.index - 2;
  if (first < 0) first = 0;
  if (first > total - 5) first = max(0, total - 5);
  int last = min(total - 1, first + 4);
  uint8_t y = 22;
  for (int i = first; i <= last; ++i, y += LINE_STEP) {
    const MenuNode& N = uiNav.currentList[i];
    const char* name = N.label;
    bool dis = nodeDisabled(N);
    if (i == uiNav.index) {
      u8g2.drawBox(MARGIN_X - 2, y - (FONT_H - 2), 128 - MARGIN_X, FONT_H);
      u8g2.setDrawColor(0);
      if (dis){ u8g2.drawStr(MARGIN_X, y, "("); u8g2.drawStr(MARGIN_X+6,y,name); u8g2.drawStr(MARGIN_X+6+u8g2.getStrWidth(name),y,")"); }
      else u8g2.drawStr(MARGIN_X, y, name);
      u8g2.setDrawColor(1);
    } else {
      if (dis){ u8g2.drawStr(MARGIN_X, y, "("); u8g2.drawStr(MARGIN_X+6,y,name); u8g2.drawStr(MARGIN_X+6+u8g2.getStrWidth(name),y,")"); }
      else u8g2.drawStr(MARGIN_X, y, name);
    }
  }
  u8g2.sendBuffer();
}

// Pantalla edición genérica (FLOAT / INT / ENUM) - placeholder simple
static void drawValueEdit(const MenuNode& N) {
  u8g2.clearBuffer();
  u8g2.drawStr(MARGIN_X, 10, N.label);
  char line[48];
  switch (N.type) {
    case MenuNodeType::VALUE_FLOAT: {
      float v = *N.data.vf.ptr;
      snprintf(line, sizeof(line), "%6.2f %s", v, N.data.vf.unit ? N.data.vf.unit : "");
    } break;
    case MenuNodeType::VALUE_INT: {
      int32_t v = *N.data.vi.ptr;
      snprintf(line, sizeof(line), "%ld %s", (long)v, N.data.vi.unit ? N.data.vi.unit : "");
    } break;
    case MenuNodeType::VALUE_ENUM: {
      uint8_t idx = *N.data.ve.ptr;
      if (idx >= N.data.ve.count) idx = 0;
      const char* lbl = N.data.ve.labels[idx];
      snprintf(line, sizeof(line), "< %s >", lbl);
    } break;
    default: snprintf(line, sizeof(line), "?"); break;
  }
  u8g2.drawStr(MARGIN_X, 34, line);
  u8g2.drawStr(MARGIN_X, 58, "Gira=ajust | Click=OK");
  u8g2.sendBuffer();
}

// Edición de rango angular (start / end / wrap)
static const MenuNode* rangeEditingNode = nullptr;
static uint8_t rangeFocus = 0; // 0=start 1=end 2=wrap 3=OK
static void drawRangeEdit(const MenuNode& N){
  u8g2.clearBuffer();
  u8g2.drawStr(MARGIN_X,10,N.label);
  char line[48];
  float start = *N.data.rd.startPtr;
  float end   = *N.data.rd.endPtr;
  bool wraps  = *N.data.rd.wrapsPtr;
  snprintf(line,sizeof(line),"Start: %6.1f", start); u8g2.drawStr(MARGIN_X,26,line);
  snprintf(line,sizeof(line),"End  : %6.1f", end);   u8g2.drawStr(MARGIN_X,38,line);
  snprintf(line,sizeof(line),"Wrap : %s", wraps?"SI":"NO"); u8g2.drawStr(MARGIN_X,50,line);
  // Indicador foco (flecha) y botón OK
  switch(rangeFocus){
    case 0: u8g2.drawStr(110,26,"> "); break;
    case 1: u8g2.drawStr(110,38,"> "); break;
    case 2: u8g2.drawStr(110,50,"> "); break;
    case 3: u8g2.drawStr(MARGIN_X,62,"[OK]"); break;
  }
  if (rangeFocus != 3) u8g2.drawStr(MARGIN_X,62,"Click=foco / Gira=ajust");
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
}

static void startRangeEdit(const MenuNode& N){
  rangeEditingNode = &N;
  rangeFocus = 0;
  uiMode = UiViewMode::EDIT_RANGE;
  encVelLastMs = 0; encVelEma = 0.0f;
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
        if (strcmp(N.label, "MASTER_DIR")==0){
          master_direction = (*N.data.ve.ptr == 0); // 0=CW
        } else if (strcmp(N.label, "S_CURVE")==0){
          Cfg.enable_s_curve = (*N.data.ve.ptr == 1);
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
        uiNav.index = (uint8_t)ni;
      }
      if (encClick) {
        const MenuNode& N = uiNav.currentList[uiNav.index];
        if (N.type == MenuNodeType::SUBMENU) {
          enterSubmenu(N);
          uiMode = UiViewMode::SUB_MENU;
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
          }
          // activar monitor si cambia a un estado dinámico
          if (state == SysState::HOMING_SEEK || state == SysState::ROTATING || state == SysState::RUNNING){
            actionMonitorActive = true;
            actionStartMs = millis();
            uiMode = UiViewMode::ACTION_EXEC;
          }
        } else if (N.type == MenuNodeType::VALUE_FLOAT || N.type == MenuNodeType::VALUE_INT || N.type == MenuNodeType::VALUE_ENUM) {
          startEdit(N);
        } else if (N.type == MenuNodeType::RANGE_DEG) {
          startRangeEdit(N);
        }
      }
    } break;

    case UiViewMode::EDIT_VALUE: {
      if (editingNode) {
        if (encDelta != 0) {
          applyDeltaToNode(*editingNode, encDelta);
        }
        if (encClick) {
          // Guardar inmediatamente al salir
          saveConfig();
          uiMode = UiViewMode::SUB_MENU; // regresa a lista actual
          editingNode = nullptr;
        }
      } else {
        uiMode = UiViewMode::SUB_MENU;
      }
    } break;

    case UiViewMode::EDIT_RANGE: {
      if (!rangeEditingNode){ uiMode = UiViewMode::SUB_MENU; break; }
      const MenuNode& N = *rangeEditingNode;
      if (encDelta != 0){
        if (rangeFocus == 0){ // start
          float v = *N.data.rd.startPtr + (float)encDelta * 1.0f; // paso 1 grado
          if (v < -360.0f) v += 360.0f; if (v > 360.0f) v -= 360.0f;
          *N.data.rd.startPtr = v;
        } else if (rangeFocus == 1){ // end
          float v = *N.data.rd.endPtr + (float)encDelta * 1.0f;
          if (v < -360.0f) v += 360.0f; if (v > 360.0f) v -= 360.0f;
          *N.data.rd.endPtr = v;
        } else if (rangeFocus == 2){ // wrap toggle cuando hay giro
          if (encDelta != 0) {
            *N.data.rd.wrapsPtr = !*N.data.rd.wrapsPtr;
          }
        }
        applyConfigToProfiles();
      }
      if (encClick){
        rangeFocus++;
        if (rangeFocus > 3){
          // Normalizar: si no wrap, forzar que travel sea secuencia simple
          // (Aquí podríamos agregar lógica de validación adicional)
          rangeEditingNode = nullptr;
          // Guardar inmediatamente al terminar edición de rango
          saveConfig();
          uiMode = UiViewMode::SUB_MENU;
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
