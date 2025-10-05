#include "ui_menu_model.h"
#include "globals.h"
#include "eeprom_store.h"
#include "homing.h"
#include "motion.h"
#include "wifi_manager.h" // IMPORTANTE: fuera del namespace App para no encapsular headers de Arduino en App::
#include "servo_manager.h"
#include "logger.h"
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
static void actServoExec();
static void actServoLiveToggle();

// ===== Submenús (se definen primero vacíos si hay dependencias cíclicas) =====

// Declaraciones para sectores (se llenarán más adelante cuando integremos edición rango)
static uint8_t sCurveEnum = 0;    // 0=OFF 1=ON

// ---------------- ENUM helpers ----------------
static const char* MASTER_DIR_LABELS[] = {"CW","CCW"};
static const char* SCURVE_LABELS[]     = {"OFF","ON"};
static float rotateMenuTargetRevs = 1.0f; // buffer UI (no altera rotateTargetRevs hasta ejecutar)
static void actDoRotate();
static const MenuNode SUBMENU_ROTAR[] = {
  {"1. Vueltas",  MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={&rotateMenuTargetRevs,-100.0f,100.0f,0.5f,"rev"}}},
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

  // bool newSC = (sCurveEnum == 1);
  // if (newSC != Cfg.enable_s_curve) {
  //   Cfg.enable_s_curve = newSC;
  //   logPrintf("CFG","S-Curve %s", newSC?"ON":"OFF");
  // }
// Planner submenu (limpio)
#include "planner.h"
static uint8_t plannerEnableEnum = 1; // 0=OFF 1=ON
static const char* ONOFF_LABELS[] = {"OFF","ON"};
static int32_t plannerBufSizeProxy = 8; // edición
static const MenuNode SUBMENU_PLANNER[] = {
  {"1. Enable", MenuNodeType::VALUE_ENUM, nullptr,0,{.ve={ &plannerEnableEnum, ONOFF_LABELS,2}}},
  {"2. Buffer", MenuNodeType::VALUE_INT,  nullptr,0,{.vi={ &plannerBufSizeProxy,4,32,1,"seg"}}},
  {"< Volver",  MenuNodeType::PLACEHOLDER,nullptr,0,{}}};

// Hook para sincronizar valores antes de mostrar y aplicar cambios después de edición
void plannerMenuSyncLoad() {
  plannerEnableEnum = Cfg.planner_enabled ? 1 : 0;
  plannerBufSizeProxy = Cfg.planner_buffer_size;
}

void plannerMenuSyncStore() {
  bool newEnable = (plannerEnableEnum == 1);
  if (newEnable != Cfg.planner_enabled) {
    Cfg.planner_enabled = newEnable;
    logPrintf("CFG","Planner %s", newEnable?"ON":"OFF");
  }
  int newBuf = plannerBufSizeProxy;
  if (newBuf < 4) newBuf = 4; if (newBuf > 32) newBuf = 32;
  if (newBuf != Cfg.planner_buffer_size) {
    Cfg.planner_buffer_size = (uint8_t)newBuf;
    App::MotionPlanner.reset((uint8_t)newBuf); // requiere API reset(size)
    logPrintf("CFG","Planner buffer=%d", newBuf);
  }
}

static uint8_t masterDirEnum = 0; // 0=CW 1=CCW (se sincroniza en init)

static const MenuNode SUBMENU_MOV[] = {
  {"1. Master Dir", MenuNodeType::VALUE_ENUM,nullptr,0,{.ve={&masterDirEnum, MASTER_DIR_LABELS, 2}}},
  {"2. S Curve",    MenuNodeType::VALUE_ENUM,nullptr,0,{.ve={&sCurveEnum,     SCURVE_LABELS,     2}}},
  {"3. Lookahead",  MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={ &Cfg.sector_lookahead_deg,0.0f,90.0f,1.0f,"deg"}}},
  {"4. DecelBoost", MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={ &Cfg.sector_decel_boost,1.0f,5.0f,0.1f,"x"}}},
  {"< Volver",      MenuNodeType::PLACEHOLDER,nullptr,0,{}}};

static const MenuNode SUBMENU_MEC[] = {
  {"1. Cm Per Rev",  MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={&Cfg.cm_per_rev,0.1f,500.0f,0.1f,"cm/rev"}}},
  {"2. Motor Steps",  MenuNodeType::VALUE_INT,  nullptr,0,{.vi={ (int32_t*)&MOTOR_FULL_STEPS_PER_REV, 20, 2000, 1, "steps"}}},
  {"3. Microstep",    MenuNodeType::VALUE_INT,  nullptr,0,{.vi={ (int32_t*)&MICROSTEPPING,1,256,1,"x"}}},
  {"4. Gear Ratio",   MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={&GEAR_RATIO,0.0f,50.0f,0.5f,"ratio"}}},
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

// ---- INTERNET (Fase 1A) ----
// SSID enum dinámico (hasta 7 resultados + placeholder índice 0)
static char WIFI_SSIDS[7][33]; // buffers persistentes (32 + null)
static const char* WIFI_LABELS[8] = {"(scan)",
  WIFI_SSIDS[0], WIFI_SSIDS[1], WIFI_SSIDS[2], WIFI_SSIDS[3], WIFI_SSIDS[4], WIFI_SSIDS[5], WIFI_SSIDS[6]
};

// ===== Servo submenu =====
static float servoAngle = 90.0f;     // [deg]
static float servoVelMmS = 10.0f;    // [mm/s] (si mm/deg no configurado, se interpreta como deg/s)
static uint8_t servoLiveEnum = 0;    // 0=OFF 1=ON
static int32_t servoMinUsVar = 544;  // proxies editables desde UI
static int32_t servoMaxUsVar = 2400;
static uint8_t servoTestEnum = 0;    // 0=OFF 1=ON
static const char* LIVE_LABELS[] = {"OFF","ON"};
static const char* TEST_LABELS[] = {"OFF","ON"};
static const MenuNode SUBMENU_SERVO[] = {
  {"1. Angulo",  MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={&servoAngle,0.0f,180.0f,1.0f,"deg"}}},
  {"2. Vel",     MenuNodeType::VALUE_FLOAT,nullptr,0,{.vf={&servoVelMmS,0.1f,200.0f,0.1f,"mm/s"}}},
  {"3. Ejecutar",MenuNodeType::ACTION,nullptr,0,{.act={actServoExec}}},
  {"4. Live",    MenuNodeType::VALUE_ENUM,nullptr,0,{.ve={ &servoLiveEnum, LIVE_LABELS, 2}}},
  {"5. Min uS",  MenuNodeType::VALUE_INT,nullptr,0,{.vi={ &servoMinUsVar, 300, 3000, 10, "us"}}},
  {"6. Max uS",  MenuNodeType::VALUE_INT,nullptr,0,{.vi={ &servoMaxUsVar, 500, 3000, 10, "us"}}},
  {"7. Test",    MenuNodeType::VALUE_ENUM,nullptr,0,{.ve={ &servoTestEnum, TEST_LABELS, 2}}},
  {"< Volver",   MenuNodeType::PLACEHOLDER,nullptr,0,{}}};
static uint8_t wifiEnumIndex = 0; // 0 = (scan) , 1..n = redes
static void actWifiScan();
static void actWifiStatus();
static const MenuNode SUBMENU_INTERNET[] = {
  {"1. Scan",     MenuNodeType::ACTION,    nullptr,0,{.act={actWifiScan}}},
  {"2. Estado",   MenuNodeType::ACTION,    nullptr,0,{.act={actWifiStatus}}},
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
  {"8. Planner",     MenuNodeType::SUBMENU, SUBMENU_PLANNER,     (uint8_t)(sizeof(SUBMENU_PLANNER)/sizeof(MenuNode)), {}},
  // {"9. Servo",       MenuNodeType::SUBMENU, SUBMENU_SERVO,       (uint8_t)(sizeof(SUBMENU_SERVO)/sizeof(MenuNode)), {}},
  {"10. Internet",   MenuNodeType::SUBMENU, SUBMENU_INTERNET,    (uint8_t)(sizeof(SUBMENU_INTERNET)/sizeof(MenuNode)), {}},
  {"11. Pesaje",     MenuNodeType::SUBMENU, SUBMENU_PESAJE,      (uint8_t)(sizeof(SUBMENU_PESAJE)/sizeof(MenuNode)), {}},
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
  plannerMenuSyncLoad();
  // Servo disabled (reverted): don't touch servo state on init
}

// Exponer funciones para ser llamadas por la UI cuando se entra/sale del submenú Planner
void uiPlannerEnter(){ plannerMenuSyncLoad(); }
void uiPlannerLeave(){ plannerMenuSyncStore(); saveConfig(); }

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

static void actServoExec(){
  // Ejecutar movimiento del servo a servoAngle con velocidad servoVelMmS
  logPrintf("SERVO","Ejecutar: ang=%.1f vel=%.1f (unid=%s)", servoAngle, servoVelMmS, "auto");
  // Aplicar rango editado antes de mover
  uint16_t mi = (uint16_t)servoMinUsVar; uint16_t ma = (uint16_t)servoMaxUsVar;
  if (mi >= 300 && ma <= 3000 && mi < ma) ServoMgr.setPulseRange(mi, ma);
  ServoMgr.setOutputEnabled(true);
  ServoMgr.setTargetAngle(servoAngle, servoVelMmS);
}

static void actServoLiveToggle(){
  ServoMgr.setLive(servoLiveEnum==1);
  logPrintf("SERVO","Live=%s", (servoLiveEnum==1)?"ON":"OFF");
}

// ---- Acciones WiFi ----
static void actWifiScan(){
  if (WifiMgr::beginScan()) {
    logPrint("WIFI","SCAN solicitado");
    uiMode = UiViewMode::WIFI_SCANNING;
  }
}
static void actWifiStatus(){
  auto st = WifiMgr::state();
  switch (st){
  case WifiMgr::State::IDLE: logPrint("WIFI","Estado: IDLE"); break;
  case WifiMgr::State::SCANNING: logPrint("WIFI","Estado: SCANNING"); break;
  case WifiMgr::State::SCAN_DONE: logPrintf("WIFI","SCAN_DONE %d redes", WifiMgr::networkCount()); break;
  case WifiMgr::State::CONNECTING: logPrint("WIFI","Estado: CONNECTING"); break;
  case WifiMgr::State::CONNECTED: logPrintf("WIFI","Conectado IP=%s", WifiMgr::ipStr()); break;
  case WifiMgr::State::FAIL: logPrint("WIFI","Estado: FAIL"); break;
  }
  // Actualizar labels cuando scan listo
  if (st == WifiMgr::State::SCAN_DONE){
    int n = WifiMgr::networkCount();
    if (n > 7) n = 7; // porque índice 0 reservado
    for (int i=0;i<n;i++){
      String s = WiFi.SSID(i);
      strncpy(WIFI_SSIDS[i], s.c_str(), 32);
      WIFI_SSIDS[i][32] = '\0';
    }
    logPrintf("WIFI","Listado redes (max 7 mostradas):");
    for (int i=0;i<n;i++){
      logPrintf("WIFI","  #%d %s", i, WIFI_SSIDS[i]);
    }
    if (n==0) logPrint("WIFI","(No se encontraron redes)" );
  }
}

} // namespace App
