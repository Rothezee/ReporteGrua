# 🤖 Firmware para Máquina Grúa "Gold Digger" (ESP32)

Este firmware está diseñado para controlar una máquina de premios (tipo grúa "Gold Digger") utilizando un microcontrolador ESP32.

El sistema gestiona la lógica de juego, el monedero, los sensores de premio, la fuerza de la pinza y la conectividad WiFi para reportar estadísticas a un servidor web. Todos los contadores y configuraciones se guardan de forma persistente en una **EEPROM I2C externa**.

## ⚙️ Características Principales

* **Lógica de Juego:** Controla el ciclo completo de juego, activado por una señal (`EPINZA`).
* **Gestión de Pagos:** Sistema de créditos y "banco" (`BANK`) que permite ajustar la lógica de premios (`PAGO <= BANK`).
* **Control de Pinza:** Fuerza de la pinza (`FUERZA`) y tiempo (`TIEMPO`) totalmente ajustables vía PWM.
* **Menú de Configuración:** Un menú de administración en el LCD permite ajustar todos los parámetros (Precio, Tiempo, Fuerza, Modo de Barrera, etc.) sin necesidad de reprogramar.
* **Persistencia de Datos:** Utiliza una **EEPROM I2C externa** (ej. 24C32) para guardar todos los contadores (PJ, PP, Banco) y configuraciones, evitando el desgaste de la memoria flash interna del ESP32.
* **Sensor de Premio:** Soporta dos modos de barrera de premios (seleccionable por menú):
    * Infrarrojo (simple).
    * Sensor Ultrasónico HC-SR04 (calibra la distancia al inicio).
* **Conectividad WiFi:**
    * Reporta estadísticas de juego (contadores, banco) a un servidor PHP/MySQL.
    * Envía un "Heartbeat" (pulso de vida) cada 60 segundos para monitoreo online.

---

## 🔌 Hardware y Conexiones

Este firmware está configurado para un **ESP32** con los siguientes periféricos.

### Componentes Requeridos

* Placa de desarrollo **ESP32**.
* **Display LCD 16x2** con adaptador I2C (Dirección `0x27`).
* **Memoria EEPROM I2C** (ej. 24C32, 24C64) (Dirección `0x50`).
* Sensores (Monedero, Barrera IR o Ultrasonido).
* Botones (para menú).
* Controlador de motor/driver para la pinza.

### Mapa de Pines (`#define`)

| Pin Lógico | Pin ESP32 | Función |
| :--- | :--- | :--- |
| **I2C** | **GPIO 21 (SDA)** | Datos I2C (para LCD y EEPROM) |
| **I2C** | **GPIO 22 (SCL)** | Reloj I2C (para LCD y EEPROM) |
| `triger` | 13 | Trigger del sensor Ultrasónico (HC-SR04) |
| `echo` | 12 | Echo del sensor Ultrasónico (HC-SR04) |
| `DATO11` | 19 | Salida (Reset para módulo SIM, según comentario) |
| `DATO7` | 14 | Entrada - Sensor Barrera Infrarroja (premio) |
| `DATO3` | 4 | **Botón Menú (Entrar / Siguiente)** |
| `DATO5` | 25 | Salida (Sin uso aparente en el código) |
| `EPINZA` | 17 | **Entrada - Señal de Juego Activo** (Inicia el juego en `LOW`) |
| `SPINZA` | 16 | **Salida PWM - Control de Fuerza de la Pinza** |
| `DATO6` | 34 | **Botón Menú (Arriba / +)** |
| `DATO10` | 35 | **Botón Menú (Abajo / -)** |
| `DATO12` | 27 | Entrada - Sensor de premio (para modo Ultrasonido) |
| `ECOIN` | 26 | Entrada - Pulsos del Monedero/Billetero |

---

## 🔧 Configuración

Antes de compilar, debes ajustar las siguientes variables en el código:

1.  **Credenciales WiFi:**
    ```c++
    const char* ssid = "MOVISTAR-WIFI6-0160";
    const char* password = "46332714";
    ```
2.  **Servidor Web:**
    ```c++
    const char* device_id = "ESP32_005"; // ID único para esta máquina
    const char* serverAddress = "[https://maquinasbonus.com/esp32_project/insert_data.php](https://maquinasbonus.com/esp32_project/insert_data.php)";
    const char* serverAddress1 = "[https://maquinasbonus.com/esp32_project/insert_heartbeat.php](https://maquinasbonus.com/esp32_project/insert_heartbeat.php)";
    ```
3.  **Direcciones I2C (si son diferentes):**
    ```c++
    #define EEPROM_I2C_ADDRESS 0x50
    LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);
    ```
4.  **Tipos de Datos (Importante):**
    Este código está escrito para un **ESP32**, donde un `int` tiene 32 bits. Sin embargo, las funciones `putEEPROM`/`getEEPROM` guardan los `int` como 16 bits. Para evitar corrupción de datos (especialmente con números negativos como el `BANK`), las variables guardadas como `int` (ej. `BANK`, `PAGO`, `FUERZA`, etc.) deben ser declaradas como `int16_t` en lugar de `int`.

---

## 🚀 Uso del Sistema

### Modo Juego
Al arrancar, la máquina calibra el sensor ultrasónico y se conecta al WiFi. La pantalla principal muestra los contadores (`PJ`/`PP`/`PA`/`BK`) o los créditos (`Credito`), según la configuración.

* `leecoin()`: Detecta pulsos en `ECOIN` para añadir créditos.
* `loop()`: El bucle principal espera la señal de `EPINZA` en `LOW` para iniciar un ciclo de juego.
* La lógica de la pinza se ejecuta, aplicando la fuerza y el tiempo configurados.
* `leerbarrera()`: Monitorea el sensor de premios. Si detecta un premio, descuenta el `PAGO` del `BANK` y actualiza contadores.

### Modo Configuración (Menú)

1.  Para entrar al menú, **mantén presionado `DATO3` (pin 4)** durante unos segundos.
2.  Usa `DATO3` para **avanzar** por las diferentes pantallas.
3.  Usa `DATO6` (pin 34) y `DATO10` (pin 35) para **incrementar o decrementar** los valores (ej. PAGO, TIEMPO, FUERZA).
4.  Al presionar `DATO3` para salir de una pantalla de ajuste, el valor se guarda automáticamente en la EEPROM externa.

---

## 🌐 Integración Web

El firmware reporta datos a dos *endpoints* PHP:

1.  **`insert_data.php`**:
    * **Cuándo se llama:** Cada vez que `PJFIJO`, `PPFIJO`, o `BANK` cambian.
    * **Datos enviados (JSON):**
        * `device_id`: ID de la máquina.
        * `dato1`: `PAGO` (Precio de la jugada).
        * `dato2`: `PJFIJO` (Partidas jugadas fijas).
        * `dato3`: `PPFIJO` (Premios pagados fijos).
        * `dato4`: `BANK` (Banco actual, puede ser negativo).

2.  **`insert_heartkey.php`**:
    * **Cuándo se llama:** Cada 60 segundos (vía `Ticker`).
    * **Datos enviados (JSON):**
        * `device_id`: ID de la máquina.
