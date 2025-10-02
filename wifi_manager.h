#pragma once
#include <Arduino.h>
#include <WiFi.h>

namespace WifiMgr {
  enum class State : uint8_t { IDLE, SCANNING, SCAN_DONE, CONNECTING, CONNECTED, FAIL };
  enum class FailReason : uint8_t { NONE, SCAN_TIMEOUT, SCAN_FAILED, CONNECT_TIMEOUT, INVALID_PASSWORD, NO_SSID, OTHER };

  void init();
  bool beginScan();                 // inicia escaneo (no bloquea completamente; usa WiFi.scanNetworks(true))
  bool canRescan();                 // true si se puede iniciar un nuevo scan (no CONNECTING activo)
  void tick();                      // llamar en loop
  void forceRefreshIfScanDone();    // helper para UI (auto-copiar SSIDs cuando haya scan listo)
  int  networkCount();              // cantidad de redes detectadas
  const char* ssidAt(int i);        // ssid index
  void selectNetwork(int idx);      // seleccionar índice activo
  void startConnect(const char* password); // conectar usando SSID seleccionado y password provista
  void cancelConnect();             // fuerza FAIL y desconecta (para timeout)
  State state();
  const char* ipStr();
  int selectedIndex();
  unsigned long lastStateChangeMs();
  FailReason failReason();
}
