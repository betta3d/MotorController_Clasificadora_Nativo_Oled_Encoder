#pragma once
#include <stdint.h>

enum class RunMode : uint8_t { CONTINUOUS, ONE_REV };

enum class SysState : uint8_t {
  UNHOMED, HOMING_SEEK, HOMING_BACKOFF, HOMING_REAPP, READY, RUNNING, ROTATING, STOPPING, FAULT
};

enum class UiScreen : uint8_t { STATUS, MENU, EDIT, CONFIRM_HOME, CONFIRM_STOP };

// Menú
enum MenuItemId : uint8_t {
  MI_START = 0, MI_HOME,
  MI_EDIT_VSLOW, MI_EDIT_VMED, MI_EDIT_VFAST, MI_EDIT_ACCEL, MI_EDIT_JERK, MI_EDIT_CMPERREV,
  MI_SAVE, MI_DEFAULTS, MI_BACK,
  MI_COUNT
};
