# MotorController Clasificadora con OLED y Encoder

Controlador de motor paso a paso ESP32 para clasificadora de huevos con interfaz OLED, encoder rotatorio y control serial completo.

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
- ✅ **Control por sectores** - 3 zonas con velocidades diferentes:
  - **LENTO** (355°-10°): Zona de tomar/soltar huevo
  - **MEDIO** (10°-180°): Zona de transporte
  - **RÁPIDO** (180°-355°): Zona de retorno vacío
- ✅ **Rotación precisa** - Comando ROTAR=N para N vueltas exactas
- ✅ **Homing automático** - Con sensor óptico y reaproximación lenta

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

### Control Básico
```bash
STATUS          # Estado completo del sistema
ROTAR=2         # Rotar 2 vueltas (+ CW, - CCW)  
STOP            # Detener movimiento
SCURVE=ON/OFF   # Habilitar/deshabilitar curvas S
```

### Configuración Velocidades
```bash
V_SLOW=5.0      # Velocidad lenta (cm/s)
V_MED=10.0      # Velocidad media (cm/s)  
V_FAST=15.0     # Velocidad rápida (cm/s)
ACCEL=50.0      # Aceleración (cm/s²)
JERK=100.0      # Jerk para curvas S (cm/s³)
```

### Parámetros Mecánicos
```bash
MOTOR_STEPS=200     # Pasos por revolución del motor
MICROSTEPPING=16    # Factor de microstepping
GEAR_RATIO=1.0      # Relación de engranajes
CM_PER_REV=25.4     # Distancia por vuelta completa
```

### Sectores Angulares
```bash
DEG_LENTO=355-10    # Sector lento (con wrap 0°)
DEG_MEDIO=10-180    # Sector medio  
DEG_RAPIDO=180-355  # Sector rápido
```

### Homing
```bash
HOMING_SEEK_DIR=CW      # Dirección inicial (CW/CCW)
HOMING_SEEK_VEL=800     # Velocidad búsqueda (pps)
HOMING_BACKOFF=3.0      # Retroceso desde sensor (°)
```

**🔤 Nota**: Todos los comandos aceptan **mayúsculas y minúsculas** indistintamente.

## 📁 Estructura del Proyecto

```
├── MotorController.ino    # Programa principal y FSM
├── commands.h/.cpp        # Procesamiento comandos serie
├── control.h/.cpp         # Control de movimiento (ISR 1kHz)
├── motion.h/.cpp          # Perfiles de velocidad y curvas S
├── globals.h/.cpp         # Variables globales y configuración
├── eeprom_store.h/.cpp    # Persistencia EEPROM
├── encoder.h/.cpp         # Interfaz encoder rotatorio  
├── oled_ui.h/.cpp         # Interface OLED y menús
├── io.h/.cpp              # Manejo I/O y botones
├── pins.h                 # Definición de pines
└── state.h                # Estados del sistema FSM
```

## 🎯 Uso Típico

### 1. Inicialización
```bash
# Al encender, configurar parámetros:
V_SLOW=4.0
V_MED=12.0  
V_FAST=20.0
SCURVE=ON
```

### 2. Homing
- Presionar botón **HOME** físico, o
- Automático al iniciar **START/STOP** sin homing previo

### 3. Operación Normal
- **START/STOP** físico: Inicia movimiento continuo por sectores
- **Encoder**: Navegar menús OLED y ajustar parámetros
- **ROTAR=N**: Rotaciones precisas para calibración

### 4. Monitoreo
```bash
STATUS    # Ver estado completo cada vez que necesites
```

## ⚙️ Configuración Inicial Recomendada

```bash
# Configuración base para clasificadora de huevos:
CM_PER_REV=31.4159          # Circunferencia plato (10cm diámetro)
V_SLOW=3.0                  # Lento para precisión  
V_MED=8.0                   # Medio para transporte
V_FAST=15.0                 # Rápido para retorno
ACCEL=40.0                  # Aceleración suave
SCURVE=ON                   # Movimiento suave
DEG_LENTO=350-20            # Zona amplia recogida/entrega
DEG_MEDIO=20-170            # Transporte 150°
DEG_RAPIDO=170-350          # Retorno 180°
```

## 🔧 Troubleshooting

### Motor no se mueve
- Verificar conexiones TMC2208
- Comprobar alimentación 12V
- Usar `STATUS` para ver velocidades aplicadas

### Rotación imprecisa  
- Ajustar `MICROSTEPPING` según driver real
- Verificar `GEAR_RATIO` si hay reductora
- Usar `ROTAR=1` para calibrar pasos por vuelta

### Homing fallido
- Verificar sensor óptico en pin correcto
- Ajustar `HOMING_SEEK_VEL` más lenta
- Cambiar `HOMING_SEEK_DIR` si busca dirección incorrecta

## 📊 Monitoreo Debug

El sistema incluye telemetría automática:
```
STATE=RUNNING | HOMED=1 | S-CURVE=ON | v=1200.0 | v_goal=800.0 | sector=MEDIO
```

## 🤝 Contribuciones

Pull requests bienvenidos. Para cambios importantes, abrir issue primero.

## 📜 Licencia

MIT License - Ver LICENSE file para detalles.

---
**Desarrollado para clasificadora automática de huevos** 🥚⚙️