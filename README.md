# MotorController Clasificadora con OLED y Encoder

Controlador de motor paso a paso ESP32 para clasificadora de huevos con interfaz OLED, encoder rotatorio y control serial completo. Sistema modular con arquitectura por comandos separados por categorías.

## 🚀 Características Principales

### Hardware Soportado
- **ESP32** como microcontrolador principal
- **TMC2208** stepper driver (microstepping 1/16)
- **NEMA17/NEMA23** motores paso a paso
- **OLED SSD1306** 128x64 display
- **Encoder rotatorio** con botón integrado
- **Sensor óptico** para homing
- **Botones físicos** HOME y START/STOP

### Control de Movimiento
- ✅ **Curvas S (S-curve)** - Control de jerk para movimiento suave
- ✅ **Control por sectores** - 4 zonas angulares con 3 velocidades diferentes:
  - **LENTO_UP** (350°-10°): Zona de tomar huevo - Velocidad LENTA
  - **MEDIO** (10°-170°): Zona de transporte - Velocidad MEDIA
  - **LENTO_DOWN** (170°-190°): Zona de dejar huevo - Velocidad LENTA  
  - **TRAVEL** (190°-350°): Zona de retorno vacío - Velocidad RÁPIDA
- ✅ **Rotación precisa** - Comando ROTAR=N para N vueltas exactas
- ✅ **Homing automático** - Con sensor óptico centralizado
- ✅ **Arquitectura modular** - Comandos organizados por categorías

### Interfaz Usuario
- 🖥️ **Display OLED** con múltiples pantallas navegables
- 🔄 **Encoder rotatorio** para navegación y ajustes
- 📱 **Control serial** completo via USB/UART
- 💾 **Configuración persistente** en EEPROM

## 🛠️ Instalación

### Dependencias Arduino IDE
```cpp
// Instalar estas librerías:
U8g2 (OLED display)
ESP32 Board Package v3.3.0+
```

### Configuración Hardware
- **Microstepping**: TMC2208 configurado a 1/16 via jumpers
- **Conexiones**: Ver `pins.h` para mapeo completo de pines
- **Alimentación**: 12V para motor, 3.3V/5V para lógica

## 💻 Comandos Serie

### 📊 Estado y Diagnóstico
```bash
STATUS              # Estado completo del sistema (tabular)
```

### 🎮 Control de Movimiento
```bash
# Rotación
ROTAR=2.5           # Rotar 2.5 vueltas (+ CW, - CCW)  
ROTAR=-1            # Rotar 1 vuelta en sentido contrario
STOP                # Detener movimiento suave
HOME                # Ejecutar homing independiente
HOMING_SWITCH=0.70  # Vueltas locales antes de invertir dirección en homing
HOMING_TIMEOUT=1.40 # Vueltas totales acumuladas antes de declarar fallo
HOMING_DEFAULTS     # Restaurar parámetros de homing a valores por defecto

# Configuración de curvas S
SCURVE=ON           # Habilitar curvas S (movimiento suave)
SCURVE=OFF          # Deshabilitar curvas S (respuesta directa)

# Dirección principal
MASTER_DIR=CW       # Establecer dirección principal clockwise
MASTER_DIR=CCW      # Establecer dirección principal counter-clockwise
```

### ⚡ Configuración de Velocidades
```bash
# Velocidades por sector (cm/s)
V_SLOW=5.0          # Velocidad sector lento
V_MED=10.0          # Velocidad sector medio  
V_FAST=15.0         # Velocidad sector rápido
V_HOME=3.0          # Velocidad para homing
V_SYSTEM=8.0        # Velocidad sistema general

# Aceleración y jerk
ACCEL=50.0          # Aceleración máxima (cm/s²)
JERK=100.0          # Jerk para curvas S (cm/s³)
```

### 🔧 Configuración Mecánica
```bash
# Motor y microstepping
MOTOR_STEPS=200     # Pasos por revolución del motor
MICROSTEPPING=16    # Factor de microstepping (TMC2208: 1,2,4,8,16)
GEAR_RATIO=1.0      # Relación de engranajes (si hay reductora)
CM_PER_REV=31.4159  # Distancia por vuelta completa (perímetro)

# Ejemplo para plato de 10cm de diámetro:
# CM_PER_REV = π × 10 = 31.4159
```

### 🎯 Sectores Angulares
```bash
# Los 4 sectores del sistema (configuración individual):
DEG_LENTO_UP=350-10       # Sector tomar huevo: 350° a 10° (wrap en 0°) - Vel LENTA
DEG_MEDIO=10-170          # Sector transporte: 10° a 170° - Vel MEDIA
DEG_LENTO_DOWN=170-190    # Sector dejar huevo: 170° a 190° - Vel LENTA
DEG_TRAVEL=190-350        # Sector retorno: 190° a 350° - Vel RÁPIDA

# Formato: DEG_[SECTOR]=INICIO-FIN
# El sistema maneja automáticamente el wrap en 0°/360° cuando INICIO > FIN
# Ejemplo: DEG_LENTO_UP=350-10 cubre desde 350° hasta 10° pasando por 0°
```

**💡 Lógica de Sectores:**
- **4 sectores angulares** que cubren 360° completos
- **3 velocidades** aplicadas: LENTO (tomar/dejar), MEDIO (transporte), RÁPIDO (retorno)
- **Sectores LENTO**: Dos zonas (UP y DOWN) comparten la misma velocidad lenta para precisión
- **Wrap automático**: Cuando inicio > fin, el sector cruza 0° automáticamente

**🔤 Nota de Comandos**: 
- Todos los comandos son **case-insensitive** (mayúsculas/minúsculas indistintas)
- Los valores decimales usan punto como separador: `3.14`
- Los comandos son procesados inmediatamente sin confirmación
- Usar `STATUS` frecuentemente para verificar cambios

## 🏗️ Arquitectura Modular

### Estructura de Comandos por Categorías
El sistema usa una arquitectura modular donde los comandos están separados por funcionalidad:

```cpp
Namespace: Comandos::
├── commands_status.cpp     # STATUS - Información del sistema
├── commands_control.cpp    # ROTAR, STOP, HOME, MASTER_DIR, SCURVE
├── commands_velocidades.cpp # V_SLOW, V_MED, V_FAST, V_HOME, V_SYSTEM, ACCEL, JERK  
├── commands_mecanica.cpp   # MOTOR_STEPS, MICROSTEPPING, GEAR_RATIO, CM_PER_REV
└── commands_sectores.cpp   # DEG_LENTO_UP, DEG_MEDIO, DEG_LENTO_DOWN, DEG_TRAVEL
```

### Sistema de Estados (FSM)
```cpp
enum class SysState {
    UNHOMED,        # Sin referencia de posición
    HOMING_SEEK,    # Buscando sensor de referencia
    READY,          # Listo para comandos
    RUNNING,        # Movimiento continuo por sectores
    ROTATING,       # Rotación específica N vueltas
    STOPPING,       # Deteniendo suavemente
    FAULT           # Error - requiere reinicio
};
```

## 📁 Estructura del Proyecto

```
MotorController/
├── MotorController.ino         # Programa principal y control FSM
│
├── commands/                   # 📂 Sistema de comandos modular
│   ├── commands.h/.cpp         # Parser principal de comandos
│   ├── commands_status.h/.cpp  # STATUS - Información sistema
│   ├── commands_control.h/.cpp # Control: ROTAR, STOP, HOME
│   ├── commands_velocidades.h/.cpp # Configuración velocidades
│   ├── commands_mecanica.h/.cpp    # Parámetros mecánicos
│   └── commands_sectores.h/.cpp    # Sectores angulares
│
├── control/                    # 📂 Control de movimiento
│   ├── control.h/.cpp          # Control tiempo real (ISR 1kHz)
│   ├── motion.h/.cpp           # Perfiles y curvas S
│   └── homing.h/.cpp           # Sistema de homing centralizado
│
├── hardware/                   # 📂 Interfaz hardware
│   ├── encoder.h/.cpp          # Encoder rotatorio
│   ├── oled_ui.h/.cpp          # Interface OLED y menús
│   ├── io.h/.cpp               # Botones y LEDs
│   └── pins.h                  # Mapeo de pines
│
├── system/                     # 📂 Sistema y configuración
│   ├── globals.h/.cpp          # Variables globales
│   ├── eeprom_store.h/.cpp     # Persistencia EEPROM
│   ├── logger.h/.cpp           # Sistema de logging
│   └── state.h                 # Estados FSM
│
└── docs/                       # 📂 Documentación
    └── README.md               # Este archivo
```

## 🎯 Guía de Uso

### 1. Configuración Inicial
```bash
# Configurar parámetros mecánicos básicos:
MOTOR_STEPS=200             # NEMA17 estándar
MICROSTEPPING=16           # TMC2208 configurado
CM_PER_REV=31.4159         # Plato de 10cm diámetro
GEAR_RATIO=1.0             # Sin reductora

# Configurar velocidades operativas:
V_SLOW=3.0                 # Precisión en toma/entrega
V_MED=10.0                 # Transporte normal
V_FAST=18.0                # Retorno rápido
V_HOME=2.0                 # Homing lento y preciso

# Configurar sectores:
DEG_LENTO_UP=350-20       # Zona amplia tomar huevo
DEG_MEDIO=20-170          # Transporte
DEG_LENTO_DOWN=170-190    # Zona dejar huevo  
DEG_TRAVEL=190-350        # Retorno vacío

# Habilitar movimiento suave:
SCURVE=ON
ACCEL=40.0
JERK=80.0
```

### 2. Homing del Sistema
```bash
# Homing manual independiente:
HOME

# O automático al iniciar rotación sin previa referencia:
ROTAR=1                    # Ejecuta homing + rotación
```

### 3. Operación de Prueba
```bash
# Verificar configuración:
STATUS

# Rotaciones de prueba:
ROTAR=0.25                 # 1/4 vuelta de prueba
ROTAR=1                    # 1 vuelta completa
ROTAR=-0.5                 # 1/2 vuelta reversa

# Cambio de dirección principal:
MASTER_DIR=CCW             # Invertir sentido principal
ROTAR=1                    # Ahora gira en sentido contrario
```

### 4. Operación Normal con Botones Físicos
- **Botón HOME**: Ejecuta homing cuando sea necesario
- **Botón START/STOP**: Alterna entre movimiento continuo y parado
- **Encoder**: Navegar menús OLED para ajustes en tiempo real

## ⚙️ Configuraciones Recomendadas

### Clasificadora de Huevos Estándar
```bash
# Hardware típico:
MOTOR_STEPS=200
MICROSTEPPING=16
GEAR_RATIO=1.0
CM_PER_REV=31.4159         # Plato 10cm

# Velocidades balanceadas:
V_SLOW=2.5                 # Muy preciso para huevos
V_MED=8.0                  # Buen throughput
V_FAST=15.0                # Retorno eficiente
V_HOME=1.5                 # Homing ultra-preciso

# Sectores optimizados:
DEG_LENTO_UP=340-30           # Zona amplia tomar ±15°
DEG_MEDIO=30-170              # Semi-vuelta transporte
DEG_LENTO_DOWN=170-200        # Zona amplia dejar ±15°  
DEG_TRAVEL=200-340            # Retorno 140°

# Movimiento suave:
SCURVE=ON
ACCEL=35.0
JERK=70.0
```

### Sistema de Alta Velocidad
```bash
# Para aplicaciones de mayor throughput:
V_SLOW=4.0
V_MED=15.0  
V_FAST=25.0
ACCEL=60.0
JERK=120.0

# Sectores más equilibrados:
DEG_LENTO_UP=355-15
DEG_MEDIO=15-165
DEG_LENTO_DOWN=165-185
DEG_TRAVEL=185-355
```

## 🔧 Troubleshooting

### ❌ Motor no se mueve
1. **Verificar estado**: `STATUS` - comprobar velocidades y estado
2. **Verificar homing**: Si `HOMED=0`, ejecutar `HOME`
3. **Hardware**: Revisar conexiones TMC2208 y alimentación 12V
4. **Microstepping**: Verificar jumpers TMC2208 coincidan con `MICROSTEPPING`

### ❌ Rotación imprecisa  
1. **Calibrar pasos**: Usar `ROTAR=1` y medir físicamente
2. **Ajustar parámetros**:
   ```bash
   MOTOR_STEPS=400          # Si es motor 0.9° en vez de 1.8°
   MICROSTEPPING=8          # Si jumpers están en 1/8
   GEAR_RATIO=5.0           # Si hay reductora 5:1
   ```
3. **Verificar cálculo**: `CM_PER_REV` debe ser exacto al perímetro real

### ❌ Homing fallido
1. **Verificar sensor**: Pin correcto y cableado del sensor óptico
2. **Ajustar dirección**: Cambiar `MASTER_DIR` si busca lado incorrecto
3. **Velocidad**: Reducir `V_HOME` para mayor precisión
4. **Umbrales**: Ajustar `HOMING_SWITCH` (cambio de sentido) y `HOMING_TIMEOUT` (falla)

## 🔄 Homing Adaptativo Bidireccional

El sistema de homing realiza una búsqueda robusta del sensor óptico en dos fases automáticas sin requerir que el usuario conozca siempre el sentido correcto inicial.

Flujo del algoritmo:
1. Arranca siempre en la dirección inversa al master (selector=false) para diversificar el primer barrido.
2. Avanza hasta detectar el sensor óptico. Si lo detecta: estabiliza, aplica offset y finaliza.
3. Si NO lo detecta tras `HOMING_SWITCH` vueltas locales (ej. 0.70): invierte dirección (selector=true) y reinicia el contador local.
4. Si el recorrido acumulado (ambas direcciones) supera `HOMING_TIMEOUT` (ej. 1.40 vueltas) sin detectar sensor: entra en FAULT.

Parámetros configurables:
```
HOMING_SWITCH=0.70   # Rango recomendado: 0.50 – 0.90
HOMING_TIMEOUT=1.40  # Debe ser >= HOMING_SWITCH * 1.1 (recomendado ~2× HOMING_SWITCH)
```

Comando de restauración:
```
HOMING_DEFAULTS      # Restaura V_HOME, T_ESTAB, DEG_OFFSET, HOMING_SWITCH, HOMING_TIMEOUT
```

Recomendaciones de ajuste:
- Si el sensor está muy cerca del inicio físico → bajar `HOMING_SWITCH` a 0.50.
- Si hay juego mecánico o holgura → aumentar `HOMING_SWITCH` a 0.80 para dar más margen inicial.
- Si el plato puede girar más de 2 vueltas completas libres → se puede subir `HOMING_TIMEOUT` (ej. 2.0) para más tolerancia.
- Si aparecen FAULTs esporádicos y el sensor está OK → revisar alimentación / ruido antes de subir límites.

Diagnóstico:
- `STATUS` muestra: switch, timeout y contador de fallas desde el arranque.
- Logs `HOME` y `HOME_DBG` indican cuándo se cambió de sentido y progreso (`LOG-HOME=ON`, `LOG-HOME_DBG=ON`).

Fallas comunes:
| Síntoma | Posible causa | Acción sugerida |
|---------|---------------|-----------------|
| FAULT recurrente rápido | `HOMING_TIMEOUT` demasiado bajo | Incrementar a 1.6–1.8 |
| Cambia de sentido muy tarde | `HOMING_SWITCH` alto | Bajar a 0.60 |
| Cambia demasiado pronto | `HOMING_SWITCH` bajo | Subir a 0.75 |
| Sensor detecta pero offset mal | `DEG_OFFSET` incorrecto | Recalcular y ajustar |

Registro de eventos:
- En FAULT se incrementa un contador interno (`Faults homing` en STATUS) útil para sesiones largas.
- Cada cambio de sentido se loguea: `Switch dir tras X vueltas locales`.

### ❌ Movimiento brusco
1. **Activar curvas S**: `SCURVE=ON`
2. **Ajustar parámetros**:
   ```bash
   ACCEL=30.0               # Reducir aceleración
   JERK=50.0                # Reducir jerk
   ```
3. **Velocidades graduales**: No saltar de V_SLOW=2 a V_FAST=30

## 📊 Monitoreo y Diagnóstico

### Comando STATUS - Información Completa
El comando `STATUS` muestra información organizada en secciones:

```
=== MOTOR CONTROLLER STATUS ===

--- ESTADO SISTEMA ---
Estado          : READY
Homed           : Sí
Posición        : 45.5°
Sector Actual   : MEDIO

--- VELOCIDADES (cm/s) ---
V_SLOW         : 3.0
V_MED          : 10.0  
V_FAST         : 18.0
V_HOME         : 2.0
V_SYSTEM       : 8.0

--- MECÁNICA ---
Motor Steps    : 200
Microstepping  : 16
Gear Ratio     : 1.0
CM per Rev     : 31.42
Total Steps/Rev: 3200

--- CONTROL ---
S-Curve        : ON
Aceleración    : 40.0 cm/s²
Jerk           : 80.0 cm/s³
Dir Principal  : CW

--- HOMING ADAPTATIVO ---
Offset         : 45.0°
Switch Dir     : 0.70 vueltas
Timeout Total  : 1.40 vueltas
Faults Homing  : 0

--- SECTORES ANGULARES ---
LENTO_UP      : 350°-10° (wrap) - Vel LENTA
MEDIO         : 10°-170° - Vel MEDIA  
LENTO_DOWN    : 170°-190° - Vel LENTA
TRAVEL        : 190°-350° - Vel RÁPIDA

=== STATUS COMPLETO ===
```

### Interpretación de Estados
- **UNHOMED**: Sistema sin referencia - requiere `HOME`
- **HOMING_SEEK**: Ejecutando homing - no interrumpir
- **READY**: Listo para comandos
- **RUNNING**: Movimiento continuo por sectores (botón START)
- **ROTATING**: Ejecutando comando `ROTAR=N`
- **STOPPING**: Deteniendo suavemente
- **FAULT**: Error - usar `STOP` y verificar hardware

## 🤝 Desarrollo y Contribuciones

### Añadir Nuevos Comandos
1. **Identificar categoría**: status, control, velocidades, mecánica, sectores
2. **Editar archivo correspondiente**: `commands_[categoria].cpp`
3. **Seguir patrón existente**:
   ```cpp
   bool procesarComandoEjemplo(const String& comando, float valor) {
       // Validación
       if (valor < 0) return false;
       
       // Aplicar cambio
       variable_global = valor;
       
       // Log y confirmación
       Serial.printf("EJEMPLO configurado: %.2f\n", valor);
       return true;
   }
   ```

### Testing y Validación
```bash
# Suite de pruebas básicas:
STATUS                     # Verificar estado inicial
HOME                       # Probar homing
ROTAR=0.25                # Cuarto de vuelta
ROTAR=1                   # Vuelta completa
ROTAR=-1                  # Vuelta reversa
MASTER_DIR=CCW            # Cambiar dirección
ROTAR=1                   # Verificar nueva dirección
SCURVE=OFF                # Probar sin curvas S
SCURVE=ON                 # Restaurar curvas S
STATUS                    # Verificar configuración final
```

Pull requests son bienvenidos. Para cambios importantes, abrir issue primero para discusión.

## 📜 Licencia

MIT License - Ver LICENSE file para detalles completos.

---
**🥚 Desarrollado para clasificadora automática de huevos con tecnología ESP32 ⚙️**

*Versión del README: 2.0 - Arquitectura Modular*

---

## 🧭 Menú OLED Jerárquico (Nueva Interfaz)

El sistema incluye un menú jerárquico navegable con el encoder (giro = mover / cambiar valor, click = entrar/aceptar/avanzar foco). Desde la pantalla STATUS un click abre el menú raíz.

```
MENU (raíz)
├─ Acciones
│  ├─ HOME          (ACTION)      → Inicia homing centralizado
│  ├─ RUN           (ACTION)      → Entra a RUNNING (si homed y READY)
│  ├─ STOP          (ACTION)      → Solicita STOPPING (RUNNING / ROTATING)
│  ├─ ROTAR         (SUBMENU)
│  │  ├─ VUeltas    (VALUE_FLOAT) → -100.00 … 100.00 rev (paso 0.25)
│  │  ├─ Ejecutar   (ACTION)      → Inicia rotación (si no homed primero homing)
│  │  └─ < Volver   (BACK)
│  ├─ SAVE          (ACTION)      → Guarda config en EEPROM
│  ├─ DEFAULTS      (ACTION)      → Restaura valores por defecto
│  └─ < Volver      (BACK)
│
├─ Movimiento
│  ├─ MASTER_DIR    (VALUE_ENUM)  → {CW, CCW}
│  ├─ S_CURVE       (VALUE_ENUM)  → {OFF, ON}
│  └─ < Volver
│
├─ Velocidades
│  ├─ V_SLOW        (VALUE_FLOAT cm/s)
│  ├─ V_MED         (VALUE_FLOAT cm/s)
│  ├─ V_FAST        (VALUE_FLOAT cm/s)
│  ├─ V_HOME        (VALUE_FLOAT cm/s)
│  └─ < Volver
│
├─ Aceleracion
│  ├─ ACCEL         (VALUE_FLOAT cm/s²)
│  ├─ JERK          (VALUE_FLOAT cm/s³)
│  └─ < Volver
│
├─ Mecanica
│  ├─ CM_PER_REV    (VALUE_FLOAT)
│  ├─ MOTOR_STEPS   (VALUE_INT)
│  ├─ MICROSTEP     (VALUE_INT)
│  ├─ GEAR_RATIO    (VALUE_FLOAT)
│  └─ < Volver
│
├─ Homing
│  ├─ DEG_OFFSET    (VALUE_FLOAT deg)
│  ├─ T_ESTAB       (VALUE_INT ms)
│  ├─ SWITCH_V      (VALUE_FLOAT turn)   (HOMING_SWITCH_TURNS)
│  ├─ TIMEOUT_V     (VALUE_FLOAT turn)   (HOMING_TIMEOUT_TURNS)
│  └─ < Volver
│
├─ Sectores
│  ├─ LENTO_UP      (RANGE_DEG: start/end/wrap)
│  ├─ MEDIO         (RANGE_DEG)
│  ├─ LENTO_DOWN    (RANGE_DEG)
│  ├─ TRAVEL        (RANGE_DEG)
│  └─ < Volver
│
└─ Pesaje (placeholder)
    ├─ STATIONS      (PLACEHOLDER - futuro: estaciones de peso)
    └─ < Volver
```

### Tipos de Nodo
| Tipo          | Descripción | Interacción |
|---------------|-------------|------------|
| SUBMENU       | Contiene hijos | Click entra / < Volver sale |
| VALUE_FLOAT   | Número flotante (min/max/step) | Girar ajusta / Click sale |
| VALUE_INT     | Entero (min/max/step) | Girar ajusta / Click sale |
| VALUE_ENUM    | Lista fija de labels | Girar cicla / Click sale |
| RANGE_DEG     | Rango angular con wrap | Ciclo foco: Start→End→Wrap→OK |
| ACTION        | Ejecuta lógica inmediata | Click ejecuta |
| PLACEHOLDER   | Sin acción (futuro o < Volver) | N/A |

### Modos de Pantalla
| Modo UI        | Se activa cuando | Contenido |
|----------------|------------------|-----------|
| STATUS          | Pantalla base | Estado sistema / ángulo / velocidad |
| MAIN_MENU/SUB_MENU | Navegación | Lista de nodos |
| EDIT_VALUE      | VALUE_* | Etiqueta + valor editable |
| EDIT_RANGE      | RANGE_DEG | Start / End / Wrap / OK (foco cíclico) |
| ACTION_EXEC     | Acción prolongada | Estado dinámico + tiempo + STOP |
| FAULT_SCREEN    | SysState::FAULT | Mensaje de falla y retorno a menú |

### Flujo EDIT_RANGE
1. Click entra a rango.
2. Foco inicial = Start: girar ±1° (wrap circular -360↔+360 simplificado).
3. Click → End: ajustar igual que Start.
4. Click → Wrap: girar (cualquier delta) alterna SI/NO.
5. Click → OK: guarda y vuelve al submenú.

### Monitor de Acción
- Se activa al lanzar HOME / ROTAR / RUN (si procede).
- Muestra tiempo desde inicio y estado actual (HOMING_SEEK, ROTATING, RUNNING, STOPPING).
- Click: STOP (si RUNNING/ROTATING) o ignorado en homing.
- Cambio a FAULT redirige a FAULT_SCREEN automáticamente.

### Recuperación de FAULT
- FAULT_SCREEN no borra la causa; revisar logs serial (`HOME`, `HOME_DBG`, `SYSTEM`).
- Click en FAULT_SCREEN retorna al menú, el estado global permanece en FAULT hasta intervención (ej. reset o lógica futura de clear).

### Extensión Futura: Pesaje
Plan para `Pesaje`:
```
Pesaje
 ├─ E1 (SUBMENU)
 │   ├─ Habilitar   (VALUE_ENUM {OFF,ON})
 │   ├─ ClaseRef    (VALUE_ENUM {SUPERX,EXTRA,A,B,C,D})
 │   ├─ RangoPeso   (RANGE_FLOAT min/max)
 │   ├─ Tara        (ACTION) → Captura offset HX711
 │   ├─ Calibrar    (ACTION) → Flujo guiado
 │   └─ < Volver
 └─ ... E2..E6
```

### Notas Internas
- Actualización inmediata: Los VALUE_* aplican cambios en vivo (se puede optimizar para diferir hasta SAVE).
- ENUMs especiales: MASTER_DIR y S_CURVE tienen efectos secundarios inmediatos (dirección y curvas S).
- Validación sectores: Próxima mejora (evitar solapes incoherentes al editar rangos).

---
*Sección agregada en rama `feature/new_menu_ui` (versión UI refactor preliminar).* 