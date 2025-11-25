# 🤖 Firmware para Máquina Grúa "Gold Digger" (ESP32) - Versión MQTT

Este firmware está diseñado para controlar una máquina de premios (tipo grúa "Gold Digger") utilizando un microcontrolador ESP32.

El sistema gestiona la lógica de juego, el monedero, los sensores de premio, la fuerza de la pinza y la conectividad WiFi/MQTT para reportar estadísticas en tiempo real. Todos los contadores y configuraciones se guardan de forma persistente en la **EEPROM interna del ESP32**.

> **Versión:** Noviembre 2025 - MQTT Optimizado con Heartbeat Corregido

---

## ⚙️ Características Principales

* **Lógica de Juego:** Controla el ciclo completo de juego, activado por una señal (`PIN_PINZA_ENABLE`).
* **Gestión de Pagos:** Sistema de créditos y "banco" (`BANK`) que permite ajustar la lógica de premios (`PAGO <= BANK`).
* **Control de Pinza:** Fuerza de la pinza (`FUERZA`) y tiempo (`TIEMPO`) totalmente ajustables vía PWM.
* **Menú de Configuración:** Un menú de administración en el LCD permite ajustar todos los parámetros (Precio, Tiempo, Fuerza, Modo de Barrera, etc.) sin necesidad de reprogramar.
* **Persistencia de Datos:** Utiliza la **EEPROM interna del ESP32** (512 bytes) para guardar todos los contadores (PJ, PP, Banco) y configuraciones.
* **Sensor de Premio:** Soporta dos modos de barrera de premios (seleccionable por menú):
    * Infrarrojo (simple).
    * Sensor Ultrasónico HC-SR04 (calibra la distancia al inicio).
* **Conectividad MQTT:**
    * 🆕 **Protocolo MQTT** en lugar de HTTP para comunicación más eficiente
    * Reporta estadísticas de juego en tiempo real a través de `broker.emqx.io`
    * Envía **Heartbeat** cada 60 segundos para monitoreo de estado
    * Sistema de reconexión automática no bloqueante
    * Optimizado para evitar bloqueos durante la comunicación

---

## 🆕 Cambios en la Versión MQTT Optimizada (Nov 2025)

### ✅ Migración de HTTP a MQTT
- **Antes:** Enviaba datos mediante peticiones HTTP POST a endpoints PHP
- **Ahora:** Publica datos en tópicos MQTT para comunicación más eficiente y en tiempo real
- **Broker:** `broker.emqx.io` (puerto 1883)

### ✅ Sistema de Heartbeat Corregido
- **Problema anterior:** El heartbeat no se enviaba correctamente
- **Solución:** Uso de flags (`volatile bool debeEnviarHeartbeat`) en lugar de llamadas directas desde `Ticker`
- **Resultado:** Heartbeat funcional cada 60 segundos sin bloquear el sistema

### ✅ Optimización de Pantalla LCD
- Solo actualiza los valores que cambian (reduce parpadeo)
- Manejo correcto de etiquetas estáticas (PJ:, PP:, PA:, BK:)
- Dos modos de visualización: Contadores o Créditos

### ✅ Corrección de Tipos de Datos en EEPROM
- `BANK` ahora es `int32_t` (puede ser negativo, requiere 4 bytes con signo)
- `COIN`, `CONTSALIDA`, `PJFIJO`, `PPFIJO` son `uint32_t` (4 bytes sin signo)
- Funciones dedicadas: `putEEPROM32()` y `putEEPROM32_signed()`

### ✅ Sistema de Reconexión No Bloqueante
- Verifica WiFi y MQTT cada 30 segundos sin detener el juego
- Reconexión automática en caso de pérdida de conexión
- `client.loop()` se ejecuta en múltiples puntos críticos

---

## 🔌 Hardware y Conexiones

Este firmware está configurado para un **ESP32** con los siguientes periféricos.

### Componentes Requeridos

* Placa de desarrollo **ESP32**
* **Display LCD 16x2** con adaptador I2C (Dirección `0x27`)
* Sensores (Monedero, Barrera IR o Ultrasonido)
* Botones (para menú de configuración)
* Controlador de motor/driver para la pinza

### Mapa de Pines

| Pin Lógico | Pin ESP32 | Función |
|:-----------|:----------|:--------|
| **I2C SDA** | **GPIO 21** | Datos I2C (para LCD) |
| **I2C SCL** | **GPIO 22** | Reloj I2C (para LCD) |
| `PIN_TRIGER` | 13 | Trigger del sensor Ultrasónico (HC-SR04) |
| `PIN_ECHO` | 12 | Echo del sensor Ultrasónico (HC-SR04) |
| `PIN_DATO11` | 19 | Salida auxiliar |
| `PIN_DATO7` | 14 | Entrada - Sensor Barrera Infrarroja |
| `PIN_DATO3` | 4 | **Botón Menú (Entrar / Siguiente)** |
| `PIN_DATO5` | 25 | Salida auxiliar |
| `PIN_PINZA_ENABLE` | 17 | **Entrada - Señal de Juego Activo** |
| `PIN_PINZA_PWM` | 16 | **Salida PWM - Control de Fuerza de la Pinza** |
| `PIN_DATO6` | 34 | **Botón Menú (Arriba / +)** |
| `PIN_DATO10` | 35 | **Botón Menú (Abajo / -)** |
| `PIN_DATO12` | 27 | Entrada - Sensor de premio (modo Ultrasonido) |
| `PIN_COIN` | 26 | Entrada - Pulsos del Monedero |

---

## 🔧 Configuración del Código

Antes de compilar, ajusta las siguientes variables:

### 1. Credenciales WiFi
```cpp
const char* ssid = "TU_RED_WIFI";
const char* password = "TU_CONTRASEÑA";
```

### 2. Configuración MQTT
```cpp
const char* device_id = "ESP32_005";        // ID único para esta máquina
const char* mqtt_server = "broker.emqx.io"; // Broker MQTT
const int mqtt_port = 1883;                 // Puerto MQTT estándar

// Tópicos MQTT (se generan automáticamente con el device_id)
const char* topic_datos = "maquinas/ESP32_005/datos";
const char* topic_estado = "maquinas/ESP32_005/estado";
const char* topic_heartbeat = "maquinas/ESP32_005/heartbeat";
```

### 3. Dirección I2C del LCD
```cpp
LiquidCrystal_I2C lcd(0x27, 16, 2); // Dirección 0x27, 16 columnas, 2 filas
```

### 4. Tamaño de EEPROM Interna
```cpp
#define EEPROM_SIZE 512 // Bytes de EEPROM interna a usar
```

---

## 📚 Librerías Requeridas

Instala estas librerías desde el **Administrador de Librerías de Arduino**:

```
- Wire (incluida con Arduino)
- LiquidCrystal_I2C
- WiFi (incluida con ESP32)
- PubSubClient (para MQTT)
- ArduinoJson (v6 o superior)
- EEPROM (incluida con ESP32)
- Ticker (incluida con ESP32)
```

---

## 🚀 Uso del Sistema

### Modo Juego

Al arrancar, la máquina:
1. Inicializa la EEPROM interna
2. Carga los contadores guardados
3. Calibra el sensor ultrasónico (si está activado)
4. Se conecta al WiFi
5. Se conecta al broker MQTT
6. Inicia el sistema de heartbeat automático

**Funcionamiento:**
- `leecoin()`: Detecta pulsos en `PIN_COIN` para añadir créditos
- `loop()`: Espera la señal de `PIN_PINZA_ENABLE` en `LOW` para iniciar juego
- La lógica de la pinza se ejecuta según configuración
- `leerbarrera()`: Monitorea el sensor de premios
- Si detecta premio: descuenta `PAGO` del `BANK` y actualiza contadores

### Modo Configuración (Menú)

1. **Mantén presionado `PIN_DATO3` (pin 4)** durante 3 segundos
2. Usa `PIN_DATO3` para **avanzar** por las pantallas
3. Usa `PIN_DATO6` (arriba) y `PIN_DATO10` (abajo) para **ajustar valores**
4. Al salir de cada pantalla, el valor se guarda automáticamente en EEPROM

**Opciones del menú:**
- Ver contadores fijos (PJ/PP)
- Borrar contadores (requiere confirmación)
- Modo de display (Contadores / Créditos)
- Ajustar PAGO (precio por jugada)
- Ajustar TIEMPO (duración de la pinza)
- Ajustar TIEMPO F. FUERTE (tiempo de fuerza máxima)
- Ajustar FUERZA (potencia de la pinza, 0-100%)
- Tipo de barrera (Infrarrojo / Ultrasonido)
- Prueba de barrera

---

## 🌐 Integración MQTT

El firmware publica datos en tres tópicos MQTT:

### 1. `maquinas/{device_id}/datos`
**Cuándo:** Cada vez que cambian `PJFIJO`, `PPFIJO`, o `BANK`

**Payload JSON:**
```json
{
  "device_id": "ESP32_005",
  "pago": 20,
  "partidas_jugadas": 150,
  "premios_pagados": 45,
  "banco": 105
}
```

### 2. `maquinas/{device_id}/estado`
**Cuándo:** Al conectarse al broker (Will Message)

**Payload:**
- `"online"` - cuando se conecta
- `"offline"` - cuando se desconecta (mensaje de última voluntad)

### 3. `maquinas/{device_id}/heartbeat`
**Cuándo:** Cada 60 segundos automáticamente

**Payload JSON:**
```json
{
  "device_id": "ESP32_005",
  "timestamp": 123456789
}
```

---

## 🔄 Sistema de Reconexión

El firmware incluye reconexión automática:

- **WiFi:** Verifica cada 30 segundos
- **MQTT:** Reconecta automáticamente si se pierde la conexión
- **No bloqueante:** El juego continúa aunque esté desconectado
- Los datos se envían cuando recupera la conexión

---

## 🗄️ Gestión de Memoria EEPROM

### EEPROM Interna del ESP32

Este firmware utiliza la **EEPROM interna del ESP32** (emulada en Flash):

```cpp
#include <EEPROM.h>

// Inicializar en setup()
EEPROM.begin(512);

// Guardar datos
EEPROM.put(address, value);
EEPROM.commit();

// Leer datos
EEPROM.get(address, variable);
```

**Ventajas:**
- ✅ No requiere hardware adicional
- ✅ 512 bytes disponibles por defecto
- ✅ Funciones `put()`/`get()` manejan cualquier tipo de dato
- ✅ Ideal para aplicaciones simples

**Mapa de memoria EEPROM:**
| Dirección | Tipo | Variable | Descripción |
|:----------|:-----|:---------|:------------|
| 1 | uint32_t | COIN | Contador de monedas total |
| 5 | uint32_t | CONTSALIDA | Contador de premios entregados |
| 9 | int32_t | BANK | Banco (puede ser negativo) |
| 13 | int16_t | PAGO | Precio por jugada |
| 17 | int16_t | TIEMPO | Tiempo de la pinza (ms) |
| 21 | int16_t | FUERZA | Fuerza de la pinza (0-100) |
| 25 | uint32_t | PJFIJO | Partidas jugadas (fijo) |
| 29 | uint32_t | PPFIJO | Premios pagados (fijo) |
| 33 | int16_t | BARRERAAUX2 | Tipo de barrera (0=IR, 1=US) |
| 37 | int16_t | GRUADISPLAY | Modo display (0=Contadores, 1=Créditos) |
| 41 | int16_t | TIEMPO5 | Tiempo fuerza fuerte (ms) |
| 45 | int16_t | INICIO | Flag de inicialización (valor: 35) |

---

## 🔧 Migración a EEPROM Externa I2C

Si necesitas usar una **EEPROM externa I2C** (como 24C32, 24C64) en lugar de la interna:

### Cambios Necesarios:

#### 1. Remover EEPROM.h y agregar librería externa
```cpp
// QUITAR:
#include <EEPROM.h>

// AGREGAR:
#include <I2C_eeprom.h>  // Librería de Rob Tillaart
```

#### 2. Configurar EEPROM externa
```cpp
#define EEPROM_I2C_ADDRESS 0x50  // Dirección típica de 24C32
I2C_eeprom eeprom(EEPROM_I2C_ADDRESS, I2C_DEVICESIZE_24LC32);
```

#### 3. Inicializar en setup()
```cpp
void setup() {
    Wire.begin();
    eeprom.begin();
    
    // Verificar si funciona
    if (!eeprom.isConnected()) {
        Serial.println("Error: EEPROM externa no detectada");
    }
}
```

#### 4. Reemplazar funciones de lectura/escritura
```cpp
// ANTES (EEPROM interna):
EEPROM.begin(512);
EEPROM.put(address, value);
EEPROM.commit();
EEPROM.get(address, variable);

// DESPUÉS (EEPROM externa):
eeprom.writeBlock(address, (uint8_t*)&value, sizeof(value));
eeprom.readBlock(address, (uint8_t*)&variable, sizeof(variable));
```

#### 5. Actualizar funciones putEEPROM
```cpp
void putEEPROM32(int address, uint32_t value) {
    uint32_t currentValue;
    eeprom.readBlock(address, (uint8_t*)&currentValue, sizeof(currentValue));
    if (currentValue != value) {
        eeprom.writeBlock(address, (uint8_t*)&value, sizeof(value));
    }
}
```

**Ventajas de EEPROM externa:**
- ✅ Mayor durabilidad (1,000,000 ciclos vs 100,000 de la Flash)
- ✅ Más capacidad disponible (32KB - 64KB típicamente)
- ✅ No consume ciclos de la Flash del ESP32
- ✅ Puede ser removida/reemplazada fácilmente

**Cuándo usar EEPROM externa:**
- ✔️ Escrituras muy frecuentes (cada minuto o menos)
- ✔️ Necesitas más de 512 bytes
- ✔️ Aplicación crítica de larga duración
- ✔️ Múltiples dispositivos compartiendo configuración

---

## 📊 Monitoreo Serial

El firmware envía información de debug por el **Serial Monitor** (115200 baud):

```
Iniciando...
Conectando a WiFi...
✓ WiFi conectado
IP: 192.168.1.100
Conectando MQTT... ✓ conectado!
✓ Setup completo

📊 Datos enviados
💓 Heartbeat enviado
Reconectando WiFi...
```

---

## 🐛 Solución de Problemas

### El heartbeat no se envía
✅ **Solucionado en esta versión** mediante el uso de flags en lugar de llamadas directas desde `Ticker`

### No se conecta al WiFi
- Verifica SSID y contraseña
- Asegúrate de estar en rango de la red
- La red debe ser 2.4GHz (ESP32 no soporta 5GHz)

### No se conecta al broker MQTT
- Verifica que `broker.emqx.io` esté accesible
- Prueba cambiar el broker a `test.mosquitto.org`
- Verifica el puerto 1883 no esté bloqueado por firewall

### Los contadores no se guardan
- Verifica que `EEPROM.begin(512)` se llame en `setup()`
- Asegúrate de llamar `EEPROM.commit()` después de cada `put()`
- La EEPROM interna tiene vida útil limitada (~100,000 escrituras)

### La pantalla LCD no funciona
- Verifica las conexiones I2C (SDA=21, SCL=22)
- Prueba cambiar la dirección I2C (común: 0x27 o 0x3F)
- Escanea el bus I2C para detectar la dirección correcta

### Datos corruptos en EEPROM
- Usa los tipos de datos correctos (`int32_t` para BANK)
- Verifica el mapa de memoria (no sobrescribir direcciones)
- En caso de corrupción, usa el menú para "Borrar Contadores"

---

## 📝 Notas de Desarrollo

### Estructura del Código

1. **Configuración y Constantes** - Pines, WiFi, MQTT
2. **Variables Globales** - Contadores, estado del juego
3. **Funciones EEPROM** - Lectura/escritura optimizada
4. **Funciones de Red** - WiFi, MQTT, heartbeat
5. **Funciones de Pantalla** - LCD optimizado
6. **Funciones de Juego** - Lógica principal
7. **Setup** - Inicialización
8. **Loop** - Ciclo principal no bloqueante

### Buenas Prácticas Implementadas

- ✅ Reconexión no bloqueante
- ✅ Optimización de escrituras EEPROM (solo si cambia)
- ✅ Actualización parcial del LCD (reduce parpadeo)
- ✅ Heartbeat mediante flags (evita bloqueos)
- ✅ Tipos de datos correctos (int32_t, uint32_t)
- ✅ Múltiples puntos de `client.loop()` para mantener MQTT activo

---

## 📄 Licencia

Este firmware es proporcionado "tal cual" para uso educativo y comercial.

---

## 🤝 Soporte

Para reportar problemas o solicitar características:
- Revisa la sección de **Solución de Problemas**
- Verifica el **Monitor Serial** para mensajes de debug
- Consulta la documentación de las librerías utilizadas

---

**Versión del Firmware:** 1.7 MQTT Optimizado (Noviembre 2025)  
**Compatible con:** ESP32 DevKit v1, ESP32-WROOM-32  
**IDE Recomendado:** Arduino IDE 2.x o PlatformIO
