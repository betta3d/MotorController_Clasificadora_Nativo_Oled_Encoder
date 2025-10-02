#pragma once
#include <stdint.h>
#include <stddef.h>

namespace App {

// Tipos de nodo para menú jerárquico
enum class MenuNodeType : uint8_t {
  SUBMENU,
  VALUE_FLOAT,
  VALUE_INT,
  VALUE_ENUM,
  RANGE_DEG,
  ACTION,
  PLACEHOLDER
};

struct MenuValueFloat { float* ptr; float minV; float maxV; float step; const char* unit; };
struct MenuValueInt   { int32_t* ptr; int32_t minV; int32_t maxV; int32_t step; const char* unit; };
struct MenuValueEnum  { uint8_t* ptr; const char** labels; uint8_t count; };
struct MenuRangeDeg   { float* startPtr; float* endPtr; bool* wrapsPtr; };
struct MenuAction     { void (*exec)(); };

struct MenuNode {
  const char* label;        // Texto mostrado
  MenuNodeType type;        // Tipo
  const MenuNode* children; // Hijos si SUBMENU
  uint8_t childCount;       // Nº hijos
  union {                   // Datos según tipo
    MenuValueFloat vf;
    MenuValueInt   vi;
    MenuValueEnum  ve;
    MenuRangeDeg   rd;
    MenuAction     act;
  } data;
};

// Acceso global a la raíz del menú (puntero a primer elemento)
extern const MenuNode* MAIN_MENU;
extern const uint8_t MAIN_MENU_COUNT;

// Estados UI extendidos para el nuevo sistema
enum class UiViewMode : uint8_t {
  MAIN_MENU,
  SUB_MENU,
  EDIT_VALUE,
  EDIT_RANGE,
  ACTION_EXEC,
  ROTATE_EDIT,
  FAULT_SCREEN,
  CONFIRM_GENERIC,
  WIFI_SCANNING,
  WIFI_LIST,
  WIFI_CONNECTING,
  WIFI_RESULT,
  WIFI_PW_EDIT
};

// Contexto de navegación
struct UiNavContext {
  const MenuNode* currentList;   // Lista actual
  uint8_t currentCount;          // Nº elementos en lista actual
  uint8_t index;                 // Índice seleccionado en lista
  const MenuNode* stack[6];      // Pila de submenús (profundidad máx 6)
  uint8_t stackCount;            // Profundidad
};

extern UiNavContext uiNav;
extern UiViewMode uiMode;

// Inicializa modelo (setea root en navegación)
void uiMenuModelInit();

} // namespace App
