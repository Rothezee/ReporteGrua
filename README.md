# 🤖 Firmware para Máquina Grúa (ESP32) - Versión MQTT Dual Memory

Este firmware está diseñado para controlar una máquina de premios tipo grúa utilizando un microcontrolador ESP32.

El sistema gestiona la lógica de juego, el monedero, los sensores de premio, la fuerza de la pinza y la conectividad WiFi/MQTT para reportar estadísticas en tiempo real a un servidor remoto.

> **Novedad Principal:** Este firmware incluye un sistema de **Memoria Híbrida Seleccionable** y un **Motor de Red Asíncrono**. Puedes elegir entre usar la memoria interna del ESP32 o una EEPROM externa I2C cambiando una sola línea de código, mientras el equipo mantiene su conexión a internet 100% estable en segundo plano.

---

## ⚙️ Características Principales

* **Protocolo MQTT Asíncrono:** Comunicación ultrarrápida que no interrumpe los ciclos de la máquina ni congela la grúa.
* **Memoria Dual (Switch):** Selector en código `#define USAR_EEPROM_EXTERNA` para alternar entre `0` (Memoria Flash Interna emulada) y `1` (Chip EEPROM I2C Externo AT24C32/64).
* **Lógica de Juego Completa:** Controla tiempos, monedas, créditos y el ciclo exacto de bajada/subida de la garra.
* **Gestión de Pagos:** Sistema de "banco" (`BANK`) que ajusta dinámicamente la fuerza de la pinza según la recaudación.
* **Control PWM:** Fuerza de la pinza ajustable (0-255) y perfiles de fuerza variable (pérdida de fuerza en el aire).
* **Menú de Configuración:** Ajustes físicos mediante pantalla LCD y botones (Precio, Tiempo, Fuerza, Tipo de Barrera).
* **Keep-Alive Constante:** Envía un heartbeat MQTT cada 60 segundos, incluso cuando la máquina está en espera de jugadores, evitando cortes del broker.
* **Reconexión Silenciosa:** Se reconecta a caídas de WiFi o MQTT en fracciones de segundo durante el tiempo de inactividad sin tragar monedas.

---

## 🔌 Hardware y Conexiones

### Mapa de Pines (WEMOS D1 R32 / ESP32)

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

### Reglas de Alimentación (¡Importante!)
Para evitar reinicios de la placa (Brownouts) causados por el alto consumo del motor de la pinza, respeta estas reglas en el armado de tu hardware o diseño de placa:
* **No usar los 5V de la fuente principal directo al ESP32:** Utiliza un módulo Step-Down (ej. LM2596) conectado a la línea de 12V/24V y ajústalo a 5.0V exactos para alimentar la lógica.
* **Filtrado de Picos:** Añade un capacitor electrolítico de al menos `470µF (16V)` entre los pines 5V y GND del ESP32 para absorber las caídas de tensión.
* **Tierras Unificadas:** Todos los componentes, monederos y fuentes deben compartir el mismo pin GND para evitar ruido lógico en los sensores.

---

## 🔧 Configuración del Firmware

Antes de cargar el código, revisa las primeras líneas del archivo `.ino`:

**1. Selector de Memoria (Vital)**
```cpp
// 0 = Memoria Interna (Para pruebas en casa sin chip)
// 1 = Memoria Externa (Para máquina real con AT24C32)
#define USAR_EEPROM_EXTERNA 1 
```

**2. Credenciales WiFi**
```cpp
const char* ssid = "TU_RED_WIFI";
const char* password = "TU_CONTRASEÑA";
```

**3. Configuración del Broker y Tópicos MQTT**
Asegúrate de cambiar `ESP32_005` por el ID único de cada máquina que armes.
```cpp
const char* mqtt_server = "broker.emqx.io"; 
const int mqtt_port = 1883;
const char* topic_datos = "maquinas/ESP32_005/datos"; 
const char* topic_heartbeat = "maquinas/ESP32_005/heartbeat";
```

---

## 📊 Tópicos MQTT y Formatos

El sistema publica eventos en tiempo real en los siguientes canales:

* **Datos de Juego (`maquinas/ESP32_005/datos`):** Se publica al finalizar cada partida si hubo cambios en los contadores. Formato JSON: `{"device_id": "...", "pago": 20, "partidas_jugadas": 100, "banco": 500}`
* **Estado / Heartbeat (`maquinas/ESP32_005/heartbeat`):** Pulso vital enviado cada 60 segundos clavados para mantener el socket abierto. Formato JSON: `{"device_id": "...", "status": "online"}`
* **Conexión LWT (`maquinas/status`):** Mensaje general de conexión del broker ("online" al iniciar sesión).

---

## 🛠️ Solución de Problemas Comunes

* **La placa se desconecta del servidor luego de un rato sin jugar:** Verifica que el bucle de espera en el código incluya la llamada a `enviarPulso()`. El broker cierra conexiones "mudas" de más de 30 minutos.
* **La pantalla LCD muestra "CONT.FIJOS" a mitad de una jugada:** Es una caída de voltaje crítica (Brownout). El motor de la pinza o un relé le robó energía al ESP32. Revisa el apartado "Reglas de Alimentación".
* **El LCD no enciende o muestra cuadros negros:** Gira el potenciómetro azul de contraste detrás del módulo I2C o verifica la dirección en el código (`0x27` o `0x3F`).
* **La máquina olvida los contadores al apagar:** Si usas Memoria Externa (`1`), verifica la conexión de los pines SDA/SCL. Si usas Memoria Interna (`0`), revisa que el switch en el código esté configurado correctamente.

---

## 📝 Notas de Versión (Febrero 2026)

* **Motor de Red No Bloqueante:** Se eliminaron los `delay()` en las rutinas de reconexión WiFi y MQTT, evitando que la máquina se "congele" o trague monedas si el router se reinicia.
* **Keep-Alive en Bucle de Espera:** Se integró la lógica de monitoreo de red y pulsos Heartbeat dentro de los bucles inactivos para evitar el baneo de brokers públicos como EMQX.
* **Protección de Datos:** Actualización del envío MQTT para reportar el conteo final de partidas de manera segura una vez que la grúa vuelve a su posición de origen.
