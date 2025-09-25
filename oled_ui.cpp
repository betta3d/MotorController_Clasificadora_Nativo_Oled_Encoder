#include "oled_ui.h"
#include "globals.h"
#include "motion.h"
#include "eeprom_store.h"

namespace App {

// -------------------- OLED INSTANCE --------------------
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ SCL, /* data=*/ SDA
);

// -------------------- UI CONFIG --------------------
static const uint8_t MARGIN_X   = 4;
static const uint8_t LINE_STEP  = 11;
static const uint8_t FONT_H     = 10;

// Screensaver variables
bool screensaverActive = false;
unsigned long screensaverStartTime = 0;
const unsigned long SCREENSAVER_DELAY_MS = 2000;

// Nombres cortos y numerados para que quepan en una línea
static const char* shortName(uint8_t id) {
  switch(id) {
    case MI_START:         return "1) RUN";
    case MI_HOME:          return "2) HOME";
    case MI_EDIT_VSLOW:    return "3) Vslow";
    case MI_EDIT_VMED:     return "4) Vmed";
    case MI_EDIT_VFAST:    return "5) Vfast";
    case MI_EDIT_ACCEL:    return "6) Accel";
    case MI_EDIT_JERK:     return "7) Jerk";
    case MI_EDIT_CMPERREV: return "8) cm/rev";
    case MI_SAVE:          return "9) Guardar";
    case MI_DEFAULTS:      return "10) Deflts";
    case MI_BACK:          return "11) Volver";
    default:               return "?";
  }
}

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

static void drawMenuScreen() {
  u8g2.clearBuffer();
  u8g2.drawStr(MARGIN_X, 10, "MENU");

  const int total = MI_COUNT;
  int first = menuIndex - 2;
  if (first < 0) first = 0;
  if (first > total - 5) first = max(0, total - 5);
  int last = min(total - 1, first + 4);

  uint8_t y = 22;
  for (int i = first; i <= last; ++i, y += LINE_STEP) {
    const char* name = shortName(menuOrder[i]);
    if (i == menuIndex) {
      u8g2.drawBox(MARGIN_X - 2, y - (FONT_H - 2), 128 - MARGIN_X, FONT_H);
      u8g2.setDrawColor(0);
      u8g2.drawStr(MARGIN_X, y, name);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(MARGIN_X, y, name);
    }
  }

  u8g2.sendBuffer();
}

static void drawEditScreen() {
  ParamRef& P = editParams[editIndex];
  u8g2.clearBuffer();
  u8g2.drawStr(MARGIN_X, 10, "EDIT");
  char line[40];
  snprintf(line, sizeof(line), "%s", P.name);
  u8g2.drawStr(MARGIN_X, 26, line);
  snprintf(line, sizeof(line), "%6.2f %s", *(P.pValue), P.unit);
  u8g2.drawStr(MARGIN_X, 42, line);
  u8g2.drawStr(MARGIN_X, 58, "Girar=ajustar | Click=OK");
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
static void doMenuAction(uint8_t id) {
  switch(id) {
    case MI_START:
      if (!homed) {
        Serial.println("[UI] Homing no realizado. Mostrando dialogo.");
        confirmIndex = 0; // Default to "No"
        uiScreen = UiScreen::CONFIRM_HOME;
        break;
      }
      if (state == SysState::READY) {
        v_goal = 0.0f; a = 0.0f;
        revStartModSteps = modSteps();
        ledVerdeBlinkState = false; ledBlinkLastMs = millis();
        state = SysState::RUNNING;
        uiScreen = UiScreen::STATUS;
        screensaverActive = false; // Ensure screensaver is off initially
        screensaverStartTime = millis(); // Start screensaver timer
        Serial.println("[UI] RUNNING iniciado.");
      } else {
        Serial.println("[UI] No se puede iniciar en este estado.\n");
      }
      break;

    case MI_HOME:
      if (state != SysState::RUNNING) {
        startHoming();
        uiScreen = UiScreen::STATUS;
        Serial.println("[UI] Homing iniciado.\n");
      } else {
        Serial.println("[UI] No se puede hacer HOME en RUNNING.\n");
      }
      break;

    case MI_EDIT_VSLOW:    editIndex = 0; uiScreen = UiScreen::EDIT; encVelLastMs=0; encVelEma=0.0f; break;
    case MI_EDIT_VMED:     editIndex = 1; uiScreen = UiScreen::EDIT; encVelLastMs=0; encVelEma=0.0f; break;
    case MI_EDIT_VFAST:    editIndex = 2; uiScreen = UiScreen::EDIT; encVelLastMs=0; encVelEma=0.0f; break;
    case MI_EDIT_ACCEL:    editIndex = 3; uiScreen = UiScreen::EDIT; encVelLastMs=0; encVelEma=0.0f; break;
    case MI_EDIT_JERK:     editIndex = 4; uiScreen = UiScreen::EDIT; encVelLastMs=0; encVelEma=0.0f; break;
    case MI_EDIT_CMPERREV: editIndex = 5; uiScreen = UiScreen::EDIT; encVelLastMs=0; encVelEma=0.0f; break;

    case MI_SAVE:
      saveConfig(); applyConfigToProfiles();
      Serial.println("[UI] Config guardada.\n");
      break;

    case MI_DEFAULTS:
      setDefaults(); applyConfigToProfiles();
      Serial.println("[UI] Defaults cargados.\n");
      break;

    case MI_BACK:
      uiScreen = UiScreen::STATUS;
      break;
  }
}

// -------------------- UI FLOW --------------------
void uiProcess(int8_t encDelta, bool encClick) {
  // If screensaver is active and there's any user interaction, disable screensaver
  if (screensaverActive && (encDelta != 0 || encClick)) {
    screensaverActive = false;
    screensaverStartTime = millis(); // Reset timer to prevent immediate re-activation
  }

  switch (uiScreen) {
    case UiScreen::STATUS:
      // Click → entra a menú (si no está RUNNING)
      if (encClick && state != SysState::RUNNING) uiScreen = UiScreen::MENU;
      // +++++ NEW: Click in RUNNING state to trigger CONFIRM_STOP +++++
      else if (encClick && state == SysState::RUNNING) {
        previousUiScreen = uiScreen; // Store current screen
        confirmIndex = 0; // Default to "No"
        uiScreen = UiScreen::CONFIRM_STOP;
      }
      break;

    case UiScreen::MENU: {
      if (encDelta != 0) {
        menuIndex = constrain(menuIndex + encDelta, 0, (int)MI_COUNT - 1);
      }
      if (encClick) {
        doMenuAction(menuOrder[menuIndex]);
      }
      // +++++ NEW: Click in MENU state (if RUNNING) to trigger CONFIRM_STOP +++++
      else if (encClick && state == SysState::RUNNING) {
        previousUiScreen = uiScreen; // Store current screen
        confirmIndex = 0; // Default to "No"
        uiScreen = UiScreen::CONFIRM_STOP;
      }
    } break;

    case UiScreen::EDIT: {
      ParamRef& P = editParams[editIndex];
      float vel = enc_update_velocity(encDelta);
      int   ad  = abs(encDelta);
      float mul = enc_velocity_multiplier(vel, ad);
      if (encDelta != 0) {
        float stepApplied = P.step * (float)encDelta * mul;
        float vNew = *(P.pValue) + stepApplied;
        if (vNew < P.minV) vNew = P.minV;
        if (vNew > P.maxV) vNew = P.maxV;
        *(P.pValue) = vNew;
        applyConfigToProfiles();
      }
      if (encClick) {
        uiScreen = UiScreen::MENU;
        encVelLastMs = 0;
        encVelEma    = 0.0f;
      }
    } break;

    case UiScreen::CONFIRM_HOME: {
      if (encDelta != 0) {
        confirmIndex = 1 - confirmIndex;
      }
      if (encClick) {
        if (confirmIndex == 1) { // "Si" was selected
          startHoming();
          uiScreen = UiScreen::STATUS;
        } else { // "No" was selected
          uiScreen = UiScreen::MENU;
        }
      }
    } break;

    case UiScreen::CONFIRM_STOP: {
      if (encDelta != 0) {
        confirmIndex = 1 - confirmIndex; // Toggle between 0 (No) and 1 (Si)
      }
      if (encClick) {
        if (confirmIndex == 1) { // "Si" was selected
          state = SysState::STOPPING; // Trigger smooth stop
          uiScreen = previousUiScreen; // Return to previous screen
          Serial.println("[UI] Deteniendo sistema por confirmacion.\n");
        } else { // "No" was selected
          uiScreen = previousUiScreen; // Return to previous screen
          Serial.println("[UI] Detencion cancelada.\n");
        }
      }
    } break;
  }
}

void uiRender() {
  // Screensaver activation logic
  if (state == SysState::RUNNING && !screensaverActive && (millis() - screensaverStartTime >= SCREENSAVER_DELAY_MS)) {
    screensaverActive = true;
  }

  if (screensaverActive) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_logisoso30_tn); // Larger font for "RUN"
    // Center "RUN" text
    uint8_t x_pos = (u8g2.getDisplayWidth() - u8g2.getStrWidth("RUN")) / 2;
    uint8_t y_pos = (u8g2.getDisplayHeight() / 2) + (u8g2.getFontAscent() / 2) - 2; // Adjust for vertical centering
    u8g2.drawStr(x_pos, y_pos, "RUN");
    u8g2.sendBuffer();
  } else {
    if (uiScreen == UiScreen::STATUS) {
      drawStatusScreen();
    } else if (uiScreen == UiScreen::MENU) {
      drawMenuScreen();
    } else if (uiScreen == UiScreen::EDIT) {
      drawEditScreen();
    } else if (uiScreen == UiScreen::CONFIRM_HOME) {
      drawConfirmHomeScreen();
    } else if (uiScreen == UiScreen::CONFIRM_STOP) {
      drawConfirmStopScreen();
    }
  }
}

} // namespace App
