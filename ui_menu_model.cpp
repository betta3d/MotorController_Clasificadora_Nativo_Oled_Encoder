#include "ui_menu_model.h"
#include "globals.h"
#include "eeprom_store.h"
#include "homing.h"
#include "motion.h"
#include <math.h>

namespace App {

UiNavContext uiNav{};
UiViewMode uiMode = UiViewMode::MAIN_MENU;

// Forward declarations acciones
static void actHome();
static void actRun();
static void actStop();
static void actSave();
static void actDefaults();

// ===== Submenús (se definen primero vacíos si hay dependencias cíclicas) =====

// Declaraciones para sectores (se llenarán más adelante cuando integremos edición rango)
extern bool dummyWrap; // placeholder si se necesita

// ---------------- ENUM helpers ----------------
static const char* MASTER_DIR_LABELS[] = {"CW","CCW"};
static const char* SCURVE_LABELS[]     = {"OFF","ON"};

// Nodos de submenús (definición parcial)
// Submenú ROTAR: editar valor (float) y ejecutar
static float rotateMenuTargetRevs = 1.0f; // buffer UI (no altera rotateTargetRevs hasta ejecutar)
static void actDoRotate();
static const MenuNode SUBMENU_ROTAR[] = {
  {"1. Vueltas",  MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={&rotateMenuTargetRevs,-100.0f,100.0f,0.25f,"rev"}}},
  {"2. Ejecutar", MenuNodeType::ACTION,nullptr,0,{.act={actDoRotate}}},
  {"< Volver",    MenuNodeType::PLACEHOLDER,nullptr,0,{}}};

static const MenuNode SUBMENU_ACCIONES[] = {
  {"1. Home",      MenuNodeType::ACTION,    nullptr,0, {.act={actHome}}},
  {"2. Run",       MenuNodeType::ACTION,    nullptr,0, {.act={actRun}}},
  {"3. Stop",      MenuNodeType::ACTION,    nullptr,0, {.act={actStop}}},
  {"4. Rotar",     MenuNodeType::SUBMENU,   SUBMENU_ROTAR,(uint8_t)(sizeof(SUBMENU_ROTAR)/sizeof(MenuNode)), {}},
  {"5. Defaults",  MenuNodeType::ACTION,    nullptr,0, {.act={actDefaults}}},
  {"< Volver",     MenuNodeType::PLACEHOLDER,nullptr,0, {}}};

static const MenuNode SUBMENU_VELOCIDADES[] = {
  {"1. V Slow", MenuNodeType::VALUE_FLOAT,nullptr,0, {.vf={&Cfg.v_slow_cmps,0.1f,50.0f,0.1f,"cm/s"}}},
  {"2. V Med",  MenuNodeType::VALUE_FLOAT,nullptr,0, {.vf={&Cfg.v_med_cmps,0.1f,100.0f,0.1f,"cm/s"}}},
  {"3. V Fast", MenuNodeType::VALUE_FLOAT,nullptr,0, {.vf={&Cfg.v_fast_cmps,0.1f,200.0f,0.1f,"cm/s"}}},
  {"4. V Home", MenuNodeType::VALUE_FLOAT,nullptr,0, {.vf={&V_HOME_CMPS,0.1f,50.0f,0.1f,"cm/s"}}},
  {"< Volver",  MenuNodeType::PLACEHOLDER,nullptr,0,{}}};

static const MenuNode SUBMENU_ACEL[] = {
  {"1. Accel", MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={&Cfg.accel_cmps2,1.0f,5000.0f,1.0f,"cm/s2"}}},
  {"2. Jerk",  MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={&Cfg.jerk_cmps3,1.0f,50000.0f,10.0f,"cm/s3"}}},
  {"< Volver", MenuNodeType::PLACEHOLDER,nullptr,0,{}}};

static uint8_t masterDirEnum = 0; // 0=CW 1=CCW (se sincroniza en init)
static uint8_t sCurveEnum = 0;    // 0=OFF 1=ON

static const MenuNode SUBMENU_MOV[] = {
  {"1. Master Dir", MenuNodeType::VALUE_ENUM,nullptr,0,{.ve={&masterDirEnum, MASTER_DIR_LABELS, 2}}},
  {"2. S Curve",    MenuNodeType::VALUE_ENUM,nullptr,0,{.ve={&sCurveEnum,     SCURVE_LABELS,     2}}},
  {"< Volver",      MenuNodeType::PLACEHOLDER,nullptr,0,{}}};

static const MenuNode SUBMENU_MEC[] = {
  {"1. Cm Per Rev",  MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={&Cfg.cm_per_rev,0.1f,500.0f,0.1f,"cm/rev"}}},
  {"2. Motor Steps",  MenuNodeType::VALUE_INT,  nullptr,0,{.vi={ (int32_t*)&MOTOR_FULL_STEPS_PER_REV, 20, 2000, 1, "steps"}}},
  {"3. Microstep",    MenuNodeType::VALUE_INT,  nullptr,0,{.vi={ (int32_t*)&MICROSTEPPING,1,256,1,"x"}}},
  {"4. Gear Ratio",   MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={&GEAR_RATIO,0.01f,50.0f,0.01f,"ratio"}}},
  {"< Volver",        MenuNodeType::PLACEHOLDER,nullptr,0,{}}};

static const MenuNode SUBMENU_HOMING[] = {
  {"1. Deg Offset",  MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={&DEG_OFFSET,-180.0f,180.0f,0.5f,"deg"}}},
  {"2. T Estab",     MenuNodeType::VALUE_INT,  nullptr,0,{.vi={(int32_t*)&TIEMPO_ESTABILIZACION_HOME,500,10000,100,"ms"}}},
  {"3. Switch V",    MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={&HOMING_SWITCH_TURNS,0.10f,5.0f,0.05f,"turn"}}},
  {"4. Timeout V",   MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={&HOMING_TIMEOUT_TURNS,0.20f,10.0f,0.10f,"turn"}}},
  {"< Volver",       MenuNodeType::PLACEHOLDER,nullptr,0,{}}};

// Sectores (placeholder: RANGE_DEG se implementará luego)
static const MenuNode SUBMENU_SECTORES[] = {
  {"1. Lento Up",    MenuNodeType::RANGE_DEG,nullptr,0,{.rd={&DEG_LENTO_UP.start,&DEG_LENTO_UP.end,&DEG_LENTO_UP.wraps}}},
  {"2. Medio",       MenuNodeType::RANGE_DEG,nullptr,0,{.rd={&DEG_MEDIO.start,&DEG_MEDIO.end,&DEG_MEDIO.wraps}}},
  {"3. Lento Down",  MenuNodeType::RANGE_DEG,nullptr,0,{.rd={&DEG_LENTO_DOWN.start,&DEG_LENTO_DOWN.end,&DEG_LENTO_DOWN.wraps}}},
  {"4. Travel",      MenuNodeType::RANGE_DEG,nullptr,0,{.rd={&DEG_TRAVEL.start,&DEG_TRAVEL.end,&DEG_TRAVEL.wraps}}},
  {"< Volver",       MenuNodeType::PLACEHOLDER,nullptr,0,{}}};

// Placeholder Pesaje (se llenará después)
static const MenuNode SUBMENU_PESAJE[] = {
  {"1. Stations", MenuNodeType::PLACEHOLDER,nullptr,0,{}},
  {"< Volver",    MenuNodeType::PLACEHOLDER,nullptr,0,{}}};

// Orden requerido por el usuario: Acciones primero
static const MenuNode MAIN_MENU_LOCAL[] = {
  {"1. Acciones",    MenuNodeType::SUBMENU, SUBMENU_ACCIONES,    (uint8_t)(sizeof(SUBMENU_ACCIONES)/sizeof(MenuNode)), {}},
  {"2. Movimiento",  MenuNodeType::SUBMENU, SUBMENU_MOV,         (uint8_t)(sizeof(SUBMENU_MOV)/sizeof(MenuNode)), {}},
  {"3. Velocidades", MenuNodeType::SUBMENU, SUBMENU_VELOCIDADES, (uint8_t)(sizeof(SUBMENU_VELOCIDADES)/sizeof(MenuNode)), {}},
  {"4. Aceleracion", MenuNodeType::SUBMENU, SUBMENU_ACEL,        (uint8_t)(sizeof(SUBMENU_ACEL)/sizeof(MenuNode)), {}},
  {"5. Mecanica",    MenuNodeType::SUBMENU, SUBMENU_MEC,         (uint8_t)(sizeof(SUBMENU_MEC)/sizeof(MenuNode)), {}},
  {"6. Homing",      MenuNodeType::SUBMENU, SUBMENU_HOMING,      (uint8_t)(sizeof(SUBMENU_HOMING)/sizeof(MenuNode)), {}},
  {"7. Sectores",    MenuNodeType::SUBMENU, SUBMENU_SECTORES,    (uint8_t)(sizeof(SUBMENU_SECTORES)/sizeof(MenuNode)), {}},
  {"8. Pesaje",      MenuNodeType::SUBMENU, SUBMENU_PESAJE,      (uint8_t)(sizeof(SUBMENU_PESAJE)/sizeof(MenuNode)), {}},
  {"< Salir",        MenuNodeType::PLACEHOLDER,nullptr,0, {}}, // salir al STATUS
};

const MenuNode* MAIN_MENU = MAIN_MENU_LOCAL;
const uint8_t MAIN_MENU_COUNT = (uint8_t)(sizeof(MAIN_MENU_LOCAL)/sizeof(MenuNode));

void uiMenuModelInit() {
  uiNav.currentList = MAIN_MENU;
  uiNav.currentCount = MAIN_MENU_COUNT;
  uiNav.index = 0;
  uiNav.stackCount = 0;
  // Sincronizar enums
  masterDirEnum = master_direction ? 0 : 1; // 0=CW 1=CCW
  sCurveEnum = Cfg.enable_s_curve ? 1 : 0;
}

// ===== Acciones básicas (implementación mínima por ahora) =====
static void actHome(){ startCentralizedHoming(); }
static void actRun(){ if (homed && state==SysState::READY){ state=SysState::RUNNING; } }
static void actStop(){ if (state==SysState::RUNNING || state==SysState::ROTATING){ state=SysState::STOPPING; } }
static void actSave(){ saveConfig(); }
static void actDefaults(){ setDefaults(); loadConfig(); }
static void actDoRotate(){
  // Guardar pedido de rotación directa (si homed, iniciar, si no, se usará lógica existente de homing previo vía comandos si luego integramos)
  if (!homed) {
    // Lanzar homing y cuando termine usuario puede volver a ejecutar
    startCentralizedHoming();
    return;
  }
  float value = rotateMenuTargetRevs;
  if (value == 0.0f) return;
  // Configurar similar a comando ROTAR (simplificado)
  applyConfigToProfiles();
  rotateTargetRevs = fabs(value);
  rotateTargetSteps = (int32_t)(fabs(value) * (float)stepsPerRev);
  rotateDirection = (value > 0);
  rotateStepsCounter = 0;
  rotateMode = true;
  setDirection(value > 0);
  state = SysState::ROTATING;
}

} // namespace App
