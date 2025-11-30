-----

# 🤖 Firmware para Máquina Grúa (ESP32) - Versión MQTT Dual Memory

Este firmware está diseñado para controlar una máquina de premios tipo grua utilizando un microcontrolador ESP32.

El sistema gestiona la lógica de juego, el monedero, los sensores de premio, la fuerza de la pinza y la conectividad WiFi/MQTT para reportar estadísticas en tiempo real a un servidor remoto.

> **Novedad Principal:** Este firmware incluye un sistema de **Memoria Híbrida Seleccionable**. Puedes elegir entre usar la memoria interna del ESP32 (para pruebas rápidas) o una EEPROM externa I2C (para producción y durabilidad) cambiando una sola línea de código.

-----

## ⚙️ Características Principales

  * **Protocolo MQTT:** Comunicación ultrarrápida y no bloqueante (reemplaza al antiguo HTTP lento).
  * **Memoria Dual (Switch):** Selector en código `#define USAR_EEPROM_EXTERNA` para alternar entre:
      * `0`: Memoria Flash Interna (Emulada).
      * `1`: Chip EEPROM I2C Externo (AT24C32/64).
  * **Lógica de Juego Completa:** Controla tiempos, monedas, créditos y ciclo de la garra.
  * **Gestión de Pagos:** Sistema de "banco" (`BANK`) que ajusta la probabilidad de premios según la recaudación.
  * **Control PWM:** Fuerza de la pinza ajustable (0-255) y perfiles de fuerza variable.
  * **Menú de Configuración:** Ajustes en pantalla LCD sin reprogramar (Precio, Tiempo, Fuerza, Tipo de Barrera).
  * **Heartbeat y Monitoreo:** Envía un pulso de "vida" cada 60 segundos y reporta estadísticas en tiempo real.

-----

## 🔌 Hardware y Conexiones

 ### Mapa de Pines (WEMOS D1 R32)

| Componente | Pin ESP32 | Función |
| :--- | :--- | :--- |
| **LCD (I2C)** | **GPIO 21** | SDA (Datos) |
| **LCD (I2C)** | **GPIO 22** | SCL (Reloj) |
| **EEPROM (I2C)** | **GPIO 21** | SDA (Compartido con LCD) |
| **EEPROM (I2C)** | **GPIO 22** | SCL (Compartido con LCD) |
| **Sensor Ultrasonido** | GPIO 13 | Trigger |
| **Sensor Ultrasonido** | GPIO 12 | Echo |
| **Pinza (Motor)** | GPIO 16 | Salida PWM (Fuerza) |
| **Pinza (Final Carrera)**| GPIO 17 | Entrada (Enable/Home) |
| **Monedero** | GPIO 26 | Entrada de Pulsos |
| **Botón Menú** | GPIO 4 | Entrar / Siguiente |
| **Botón Arriba** | GPIO 34 | Incrementar valor |
| **Botón Abajo** | GPIO 35 | Decrementar valor |

### Dirección I2C

  * **LCD:** Usualmente `0x27` (o `0x3F`).
  * **EEPROM Externa:** Usualmente `0x50`.

-----

## 🔧 Configuración del Firmware

Antes de cargar el código, revisa las primeras líneas del archivo `.ino`:

### 1\. Selector de Memoria (Vital)

Elige dónde guardar los contadores.

```cpp
// 0 = Memoria Interna (Para pruebas en casa sin chip)
// 1 = Memoria Externa (Para máquina real con AT24C32)
#define USAR_EEPROM_EXTERNA 1 
```

### 2\. Credenciales WiFi

```cpp
const char* ssid = "TU_RED_WIFI";
const char* password = "TU_CONTRASEÑA";
```

### 3\. Configuración del Broker MQTT

Configura tu servidor (público o privado).

```cpp
const char* mqtt_server = "broker.emqx.io"; // O tu IP: 82.29.x.x
const int mqtt_port = 1883;
```
### 4\. Configuración de los Topicos MQTT

```cpp
const char* topic_datos = "maquinas/ESP32_005/datos"; //TAMBIEN DEBE PONERSE LA ID DEL DISPOSITIVO ACA
const char* topic_heartbeat = "maquinas/ESP32_005/heartbeat";
```
-----

## 📊 Tópicos MQTT

El sistema publica información en los siguientes canales (tópicos):

1.  **Datos de Juego:** `maquinas/ESP32_005/datos`

      * Se envía cada vez que cambia el dinero o se juega una partida.
      * **JSON:** `{"device_id": "...", "pago": 20, "partidas_jugadas": 100, "banco": 500}`

2.  **Estado (Heartbeat):** `maquinas/ESP32_005/heartbeat`

      * Se envía cada 60 segundos para confirmar que la máquina está online.
      * **JSON:** `{"device_id": "...", "status": "online"}`

3.  **Conexión:** `maquinas/status`

      * Mensaje "online" al conectar y "offline" (LWT) si se corta la luz o el internet.

-----

## 🛠️ Solución de Problemas Comunes

### El LCD no enciende o muestra cuadros negros

  * Gira el potenciómetro azul detrás del módulo I2C del LCD.
  * Verifica si la dirección en el código es `0x27` o `0x3F`.

### La máquina olvida los contadores al apagar

  * Si usas **Memoria Externa (`1`):** Verifica que el chip AT24C32 esté conectado a los pines 21 y 22.
  * Si usas **Memoria Interna (`0`):** Asegúrate de que la línea `#define USAR_EEPROM_EXTERNA 0` esté configurada.

### Lag o lentitud en el juego

  * Si tienes configurado `#define USAR_EEPROM_EXTERNA 1` pero **NO** tienes el chip conectado, el sistema intentará hablarle a un "fantasma", causando pausas de milisegundos.
  * **Solución:** Conecta el chip o cambia al modo `0`.

-----

## 📝 Notas de Versión (Noviembre 2025)

  * **Migración a MQTT:** Se eliminó `HTTPClient` para evitar bloqueos de red.
  * **Watchdog de Red:** El sistema se reconecta automáticamente al WiFi/MQTT si se pierde la señal, sin detener el juego.
  * **Compatibilidad Retroactiva:** Se mantuvieron las funciones de lógica de juego originales para asegurar que la mecánica de la grúa no cambie.
  * **Optimización I2C:** Gestión eficiente del bus para permitir LCD y EEPROM simultáneos.

-----
