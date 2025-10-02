#include "wifi_manager.h"
#include "logger.h"
#include "globals.h"
#include "eeprom_store.h"

namespace WifiMgr {
  static State wifiState = State::IDLE;
  static FailReason lastFail = FailReason::NONE;
  static int lastScanRequestMs = 0;
  static int scanCompleteMs = 0;
  static int selIndex = -1;
  static String ipCached;
  static String lastPassword; // password dinámica provista por UI
  static unsigned long stateChangeMs = 0;           // timestamp última transición
  static unsigned long lastProgressLogMs = 0;       // para logs periódicos
  // Timeouts (ms)
  static const unsigned long SCAN_TIMEOUT_MS = 15000;      // 15s máximo escaneo (extendido)
  static const unsigned long CONNECT_TIMEOUT_MS = 12000;  // 12s intento conectar

  void init(){
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(100);
    wifiState = State::IDLE;
    stateChangeMs = millis();
    lastFail = FailReason::NONE;
    App::logPrint("WIFI","Init STA listo");
    // Intentar autoconectar si hay credenciales válidas
    if (App::Cfg.wifi_valid == 1 && App::Cfg.wifi_ssid[0] && App::Cfg.wifi_pass[0]){
      App::logPrintf("WIFI","Autoconnect a %s ...", App::Cfg.wifi_ssid);
      WiFi.begin(App::Cfg.wifi_ssid, App::Cfg.wifi_pass);
      wifiState = State::CONNECTING;
      stateChangeMs = millis();
      lastProgressLogMs = 0;
      lastPassword = App::Cfg.wifi_pass;
      selIndex = -1; // no corresponde a listado actual
    }
  }

  bool beginScan(){
    if (wifiState == State::SCANNING) return false;
    WiFi.disconnect(true);
    int n = WiFi.scanNetworks(true /*async*/);
    (void)n; // valor inicial puede ser -1 (escaneo en progreso)
    wifiState = State::SCANNING;
    stateChangeMs = millis();
    lastScanRequestMs = millis();
    selIndex = -1; // reset selección
    lastFail = FailReason::NONE;
  App::logPrint("WIFI","SCAN iniciado (async)");
    return true;
  }

  bool canRescan(){
    // Permitimos re-scan salvo que estemos CONNECTING (para no cortar un intento de conexión)
    return wifiState != State::CONNECTING;
  }

  int networkCount(){
    int raw = WiFi.scanComplete();
    if (raw < 0) return 0;
    // Contar únicos y no vacíos
    int unique = 0;
    for (int i=0;i<raw;i++){
      String ssid = WiFi.SSID(i); ssid.trim(); if (ssid.length()==0) continue;
      bool dup=false; for (int k=0;k<i;k++){ String prev=WiFi.SSID(k); prev.trim(); if (prev.equalsIgnoreCase(ssid)){ dup=true; break; } }
      if (!dup) unique++;
    }
    return unique;
  }

  const char* ssidAt(int i){
    int raw = WiFi.scanComplete(); if (raw < 0) return "";
    int logical = -1;
    static String cache; // devolver puntero estable mientras tanto
    for (int idx=0; idx<raw; idx++){
      String ssid = WiFi.SSID(idx); ssid.trim(); if (ssid.length()==0) continue;
      bool dup=false; for (int k=0;k<idx;k++){ String prev=WiFi.SSID(k); prev.trim(); if (prev.equalsIgnoreCase(ssid)){ dup=true; break; } }
      if (dup) continue;
      logical++;
      if (logical == i){ cache = ssid; return cache.c_str(); }
    }
    return "";
  }

  void selectNetwork(int idx){
    int cnt = networkCount();
    if (idx >=0 && idx < cnt){
      selIndex = idx;
      App::logPrintf("WIFI","SSID seleccionado idx=%d %s", idx, ssidAt(idx));
    }
  }

  void startConnect(const char* password){
    if (selIndex < 0){ App::logPrint("WIFI","No hay SSID seleccionado"); return; }
    if (wifiState == State::CONNECTING) return;
    // Necesitamos traducir índice lógico a índice físico original
    int raw = WiFi.scanComplete();
    int logical = -1; int physical = -1;
    for (int idx=0; idx<raw; idx++){
      String ssid = WiFi.SSID(idx); ssid.trim(); if (ssid.length()==0) continue;
      bool dup=false; for (int k=0;k<idx;k++){ String prev=WiFi.SSID(k); prev.trim(); if (prev.equalsIgnoreCase(ssid)){ dup=true; break; } }
      if (dup) continue;
      logical++;
      if (logical == selIndex){ physical = idx; break; }
    }
    if (physical < 0){ App::logPrint("WIFI","Indice logico no mapeado"); return; }
    String ssid = WiFi.SSID(physical);
  if (!password || strlen(password)==0){ App::logPrint("WIFI","Password vacia"); return; }
  lastPassword = password;
  App::logPrintf("WIFI","Conectando a %s ...", ssid.c_str());
    WiFi.disconnect(true);
    delay(20);
    wifiState = State::CONNECTING;
    stateChangeMs = millis();
    lastProgressLogMs = 0;
    WiFi.begin(ssid.c_str(), lastPassword.c_str());
  }

  void cancelConnect(){
    if (wifiState == State::CONNECTING){
      WiFi.disconnect(true);
      wifiState = State::FAIL;
      stateChangeMs = millis();
      lastFail = FailReason::CONNECT_TIMEOUT;
      App::logPrint("WIFI","Conexión cancelada (timeout)");
    }
  }

  State state(){ return wifiState; }

  unsigned long lastStateChangeMs(){ return stateChangeMs; }

  const char* ipStr(){
    if (wifiState == State::CONNECTED){ ipCached = WiFi.localIP().toString(); return ipCached.c_str(); }
    return "-";
  }

  int selectedIndex(){ return selIndex; }

  void tick(){
    // Gestionar transición de SCANNING a SCAN_DONE
    unsigned long now = millis();
    // SCANNING handling
    if (wifiState == State::SCANNING){
      int res = WiFi.scanComplete();
      if (res >= 0){
        wifiState = State::SCAN_DONE;
        stateChangeMs = now;
        scanCompleteMs = now;
        App::logPrintf("WIFI","SCAN bruto: %d entradas", res);
        // Filtrar SSIDs duplicados y vacíos
        int uniqueCount = 0;
        for (int i=0;i<res;i++){
          String ssid = WiFi.SSID(i);
          ssid.trim();
          if (ssid.length()==0) continue; // ignorar vacíos
          bool dup = false;
          for (int j=0;j<uniqueCount;j++){
            if (WiFi.SSID(j).equalsIgnoreCase(ssid)){ dup = true; break; }
          }
          if (dup) continue;
          // Para mantener orden estable, si no es duplicado pero su slot original j!=i, intercambiamos
          if (uniqueCount != i){
            // No hay API oficial para reordenar internamente la lista de scan.
            // Estrategia: solo contamos y listamos luego por contenido comparando contra listado previo.
            // Limitación: WiFi.scanComplete() entrega índice físico; no podemos reordenar. Así que almacenamos mapping.
          }
          uniqueCount++;
        }
        // Log listado único
        App::logPrintf("WIFI","Redes unicas no vacias: %d", uniqueCount);
        for (int i=0;i<res;i++){
          String ssid = WiFi.SSID(i); ssid.trim();
          if (ssid.length()==0) continue;
          // verificar si es primer aparición
          bool first = true;
          for (int k=0;k<i;k++){ String prev = WiFi.SSID(k); prev.trim(); if (prev.equalsIgnoreCase(ssid)){ first=false; break; } }
          if (!first) continue;
          App::logPrintf("WIFI","  - %s", ssid.c_str());
        }
      } else if (res == WIFI_SCAN_FAILED){
        wifiState = State::FAIL;
        stateChangeMs = now;
        lastFail = FailReason::SCAN_FAILED;
        App::logPrint("WIFI","SCAN falló");
      } else {
        // res == -1 (en progreso)
        if (now - stateChangeMs > SCAN_TIMEOUT_MS){
          wifiState = State::FAIL;
          stateChangeMs = now;
          lastFail = FailReason::SCAN_TIMEOUT;
          App::logPrint("WIFI","SCAN timeout");
        } else if (lastProgressLogMs == 0 || now - lastProgressLogMs > 1500){
          lastProgressLogMs = now;
          App::logPrint("WIFI","SCANNING...");
        }
      }
    }
    // CONNECTING handling
    if (wifiState == State::CONNECTING){
      wl_status_t st = WiFi.status();
      if (st == WL_CONNECTED){
        wifiState = State::CONNECTED;
        stateChangeMs = now;
        App::logPrintf("WIFI","CONNECTED IP=%s", WiFi.localIP().toString().c_str());
        // Guardar credenciales (SSID y password) en config persistente
        if (selIndex >= 0){
          const char* selSsid = ssidAt(selIndex);
          if (selSsid && *selSsid){
            strncpy(App::Cfg.wifi_ssid, selSsid, sizeof(App::Cfg.wifi_ssid)-1);
            App::Cfg.wifi_ssid[sizeof(App::Cfg.wifi_ssid)-1]='\0';
            strncpy(App::Cfg.wifi_pass, lastPassword.c_str(), sizeof(App::Cfg.wifi_pass)-1);
            App::Cfg.wifi_pass[sizeof(App::Cfg.wifi_pass)-1]='\0';
            App::Cfg.wifi_valid = 1;
            App::saveConfig();
            App::logPrint("WIFI","Credenciales guardadas en EEPROM");
          }
        }
      } else if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL){
        wifiState = State::FAIL;
        stateChangeMs = now;
        if (st == WL_CONNECT_FAILED){
          lastFail = FailReason::INVALID_PASSWORD;
          App::logPrint("WIFI","Conexión fallida (password)");
        } else if (st == WL_NO_SSID_AVAIL){
          lastFail = FailReason::NO_SSID;
          App::logPrint("WIFI","Conexión fallida (SSID no disponible)");
        } else {
          lastFail = FailReason::OTHER;
        }
      } else {
        if (now - stateChangeMs > CONNECT_TIMEOUT_MS){
          cancelConnect();
        } else if (lastProgressLogMs == 0 || now - lastProgressLogMs > 2000){
          lastProgressLogMs = now;
          App::logPrint("WIFI","CONNECTING...");
        }
      }
    }
  }

  void forceRefreshIfScanDone(){
    // Placeholder: en Fase 1A la UI llama a Estado manualmente; aquí podríamos poner lógica en el futuro.
  }

  FailReason failReason(){ return lastFail; }
}
