#pragma once

// TB6600 (cátodo común: PUL+/DIR+/ENA+ → +5V)
constexpr int PIN_STEP = 25;   // PUL-
constexpr int PIN_DIR  = 26;   // DIR-
constexpr int PIN_ENA  = 27;   // ENA-

// Entradas
constexpr int PIN_OPT_HOME   = 32; // Sensor óptico (LOW activo)
constexpr int PIN_BTN_HOME   = 33; // HOME (a GND) INPUT_PULLUP
constexpr int PIN_BTN_START  = 4;  // START/STOP (a GND) INPUT_PULLUP

// LEDs
constexpr int PIN_LED_ROJO   = 14; // NO listo
constexpr int PIN_LED_VERDE  = 12; // READY / parpadeo RUNNING

// Encoder
constexpr int PIN_ENC_A      = 16; //CLOCK
constexpr int PIN_ENC_B      = 17; //DT
constexpr int PIN_ENC_BTN    = 13; //SW

// Buzzer (piezo pasivo o activo) - conectar entre PIN_BUZZER y GND
// Recomendado: resistencia serie 100-220 ohms + opcional capacitor 100nF a GND cerca del buzzer para reducir picos EMI.
// Migrado a GPIO18 (no strapping) para mayor confiabilidad de tonos repetidos.
constexpr int PIN_BUZZER     = 18;

// I2C (OLED)
constexpr int I2C_SDA = 21;
constexpr int I2C_SCL = 22;
