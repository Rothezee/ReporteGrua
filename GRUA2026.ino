// Octubre 2026
// Protocolo: MQTT
// Memoria: Seleccionable (Interna ESP32 o Externa I2C)

// ==========================================
// 🎚️ CONFIGURACIÓN DE MEMORIA (TOCA AQUÍ)
// ==========================================
// 0 = Memoria Interna (Para pruebas sin chip)
// 1 = Memoria Externa (Para producción con AT24C32)
#define USAR_EEPROM_EXTERNA 0 

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Ticker.h>
#include <EEPROM.h> // Necesaria para el modo interno

// Dirección I2C de la EEPROM externa
#define EEPROM_I2C_ADDRESS 0x50
#define EEPROM_SIZE_INTERNAL 512 // Tamaño para emulación en flash

// --- CONFIGURACIÓN WIFI Y BROKER ---
const char* DNI_ADMIN = "00000000";
const int TIPO_MAQUINA = 2;

const char* ssid = "bonusplay2";
const char* password = "86678216";

const char* mqtt_server = "mqtt.eclipseprojects.io";
const int mqtt_port = 1883;

char topic_datos[48];
char topic_heartbeat[52];

// --- PINES ---
#define triger 13
#define echo 12
#define DATO11 19
#define DATO7 14
#define DATO3 4
#define DATO5 25
#define EPINZA 17
#define SPINZA 16
#define DATO6 34
#define DATO10 35
#define DATO12 26
#define ECOIN 27

#define EEPROM_INIT_CHECK_VALUE 35
#define EEPROM_FICHAS_POR_JUEGO 49
#define EEPROM_MODO_MAQUINA 53
#define EEPROM_FICHAS_TOTAL 57
#define EEPROM_ID_DISPOSITIVO 61
#define ID_DISPOSITIVO_MIN 1
#define ID_DISPOSITIVO_MAX 9999
#define PWM_FREQUENCY 100

// Modos de conteo
#define MODO_COIN 0     // fichas suman PJ directo (sin pinza)
#define MODO_CONTROL 1  // fichas suman credito; EPINZA juega y descuenta

int lcdColumns = 16;
int lcdRows = 2;
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);

WiFiClient espClient;
PubSubClient client(espClient);
Ticker tickerPulso;

// --- VARIABLES GLOBALES ---
unsigned long X = 0;
int RDISTANCIA = 0;
int TIEMPO = 0;
int TIEMPOAUX = 0;
int B = 0;
int A = 0;
int16_t PAGO = 20;
int BARRERA = 0;
int CLEAR = 0;
unsigned int COIN;
int16_t BANK = 0;
unsigned int BANKTIEMPO = 0;
unsigned int CONTSALIDA;
unsigned int Y;
int Z = 0;
int distancia = 0;
int tiempo = 0;
unsigned int CTIEMPO = 0;
int AUX = 0;
int monedasAcumuladas = 0;
// Pulsos ISR: 1 ficha = 1 pulso HIGH→LOW→HIGH (re-arm en HIGH, sin lockout largo)
volatile uint32_t coinPulsesPendientes = 0;
volatile bool coinArmed = true;
volatile unsigned long lastCoinEdgeUs = 0;
const unsigned long COIN_DEBOUNCE_US = 10000; // 10 ms anti-rebote entre flancos
int16_t FICHAS_POR_JUEGO = 1;
int16_t MODO_MAQUINA = MODO_COIN;
int16_t ID_DISPOSITIVO = 1;
char CODIGO_HARDWARE[6] = "1"; // ID numerico (client ID MQTT y topics)
int CREDITO = 0;
unsigned long lastPinzaTick = 0;
bool epinzaArmado = false;
int16_t FUERZA = 50;
int16_t INICIO = 0;
int BORRARCONTADORES = LOW;
unsigned int PJFIJO = 0;
unsigned int PPFIJO = 0;
unsigned int FICHAS_TOTAL = 0;
int BARRERAAUX = 0;
float FUERZAAUX = 0;
float FUERZAV = 0;
int TIEMPOAUX1 = 0;
int FUERZAAUX2 = 0;
int16_t GRUADISPLAY = 0;
int16_t BARRERAAUX2 = 0;
bool premioCoinPendiente = false;
bool infrarrojoDetectando = false;
unsigned long infrarrojoDesde = 0;
const unsigned long INFRARROJO_CONFIRMACION_MS = 50;
char DAT;
int TIEMPO7 = 0;
unsigned long TIEMPO8 = 0;
int TIEMPO9 = 0;
int AUXSIM = 0;
int16_t TIEMPO5 = 0;
int prevPJFIJO = 0;
int prevPPFIJO = 0;
int prevBANK = 0;
int prevPAGO = 0;
int prevFICHAS_TOTAL = 0;
int AUXDATO3 = 0;

unsigned long lastTelemetriaEnvio = 0;
int lastTelPago = -1;
int lastTelCoin = -1;
int lastTelPremios = -1;
int lastTelBanco = -1;
int lastTelCoins = -1;
const unsigned long TELEMETRIA_DEDUP_MS = 3000;

volatile bool debeEnviarHeartbeat = false;
unsigned long lastWifiBeginAttempt = 0;
// Solo forzar WiFi.begin() si sigue caido tras este tiempo (evita pelear con auto-reconnect)
const unsigned long WIFI_RETRY_MS = 30000;

bool pantallaSucia = true;
unsigned long lastMqttReconnectAttempt = 0;

bool eepromNeedsCommit = false;

void marcarPantalla() { pantallaSucia = true; }

void flushEEPROM() {
#if USAR_EEPROM_EXTERNA == 0
    if (eepromNeedsCommit) {
        EEPROM.commit();
        eepromNeedsCommit = false;
    }
#endif
}

void resetEstadoTelemetria() {
    lastTelPago = -1;
    lastTelCoin = -1;
    lastTelPremios = -1;
    lastTelBanco = -1;
    lastTelCoins = -1;
    lastTelemetriaEnvio = 0;
}

// ============================================================================
// FUNCIONES DE MEMORIA HÍBRIDA (INTERNA / EXTERNA)
// ============================================================================

// --- FUNCIONES DE BAJO NIVEL PARA EEPROM EXTERNA ---
void writeEEPROM(int address, byte data) {
  // Leer el valor actual
  byte deviceAddress = EEPROM_I2C_ADDRESS | ((address >> 8) & 0x01);  // Bit A8 en dirección I2C
  Wire.beginTransmission(deviceAddress);
  Wire.write((byte)(address & 0xFF));
  Wire.endTransmission();
  Wire.requestFrom(deviceAddress, (byte)1);
  
  byte current = 0xFF;
  if (Wire.available()) {
    current = Wire.read();
  }
  
  // Si es diferente, escribir
  if (current != data) {
    Wire.beginTransmission(deviceAddress);
    Wire.write((byte)(address & 0xFF));  // Dirección interna (0-255)
    Wire.write(data);
    Wire.endTransmission();
    delay(5);  // Tiempo típico de escritura
  }
}

byte readEEPROM(int address) {
  byte data = 0xFF;
  byte deviceAddress = EEPROM_I2C_ADDRESS | ((address >> 8) & 0x01);  // Bit A8
  Wire.beginTransmission(deviceAddress);
  Wire.write((byte)(address & 0xFF));
  Wire.endTransmission();
  Wire.requestFrom(deviceAddress, (byte)1);
  if (Wire.available()) {
    data = Wire.read();
  }
  return data;
}

// --- FUNCIONES DE ALTO NIVEL (SELECTOR AUTOMÁTICO) ---

// Escribir Entero (16 bits)
void putEEPROM(int address, int value) {
  if (USAR_EEPROM_EXTERNA == 1) {
    // Lógica Externa con tus funciones
    writeEEPROM(address, (value >> 8) & 0xFF);     // MSB
    writeEEPROM(address + 1, value & 0xFF);        // LSB
  } else {
    // Lógica Interna (Librería EEPROM)
    int16_t current;
    EEPROM.get(address, current);
    if (current != value) {
      EEPROM.put(address, (int16_t)value);
      eepromNeedsCommit = true;
    }
  }
}

// Escribir Unsigned Int (32 bits)
void putEEPROMUint(int address, unsigned int value) {
  if (USAR_EEPROM_EXTERNA == 1) {
    // Lógica Externa con tus funciones
    writeEEPROM(address, (value >> 24) & 0xFF);    // MSB
    writeEEPROM(address + 1, (value >> 16) & 0xFF);
    writeEEPROM(address + 2, (value >> 8) & 0xFF);
    writeEEPROM(address + 3, value & 0xFF);        // LSB
  } else {
    // Lógica Interna
    uint32_t current;
    EEPROM.get(address, current);
    if (current != value) {
      EEPROM.put(address, (uint32_t)value);
      eepromNeedsCommit = true;
    }
  }
}

// Leer Entero (16 bits)
int getEEPROM(int address) {
  int value = 0;
  if (USAR_EEPROM_EXTERNA == 1) {
    // Lógica Externa con tus funciones
    value = (readEEPROM(address) << 8) | readEEPROM(address + 1);
  } else {
    // Lógica Interna
    int16_t val16;
    EEPROM.get(address, val16);
    value = val16;
  }
  return value;
}

// Leer Unsigned Int (32 bits)
unsigned int getEEPROMUint(int address) {
  unsigned int value = 0;
  if (USAR_EEPROM_EXTERNA == 1) {
    // Lógica Externa con tus funciones
    value = ((unsigned int)readEEPROM(address) << 24) |
            ((unsigned int)readEEPROM(address + 1) << 16) |
            ((unsigned int)readEEPROM(address + 2) << 8) |
            (unsigned int)readEEPROM(address + 3);
  } else {
    // Lógica Interna
    uint32_t val32;
    EEPROM.get(address, val32);
    value = val32;
  }
  return value;
}

// ============================================================================
// RED Y MQTT
// ============================================================================

bool enviarTelemetriaGrua();
bool enviarTelemetriaSiNuevaPartida();
bool enviarHeartbeat();
void mantenerRed();
void delayConRed(unsigned long ms);
void leecoin();
void IRAM_ATTR coinIsr();
void programar();
void reconnectMQTT();
void rearmarBarreraSiLibre();
void leerbarrera();

void construirTopicsMqtt() {
    snprintf(topic_datos, sizeof(topic_datos), "maquinas/%s/datos", CODIGO_HARDWARE);
    snprintf(topic_heartbeat, sizeof(topic_heartbeat), "maquinas/%s/heartbeat", CODIGO_HARDWARE);
}

void aplicarIdDispositivo() {
    if (ID_DISPOSITIVO < ID_DISPOSITIVO_MIN || ID_DISPOSITIVO > ID_DISPOSITIVO_MAX) {
        ID_DISPOSITIVO = ID_DISPOSITIVO_MIN;
    }
    snprintf(CODIGO_HARDWARE, sizeof(CODIGO_HARDWARE), "%d", (int)ID_DISPOSITIVO);
    construirTopicsMqtt();
}

bool telemetriaEsDuplicada(int pago, int coin, int premios, int banco, int coins) {
    if (pago != lastTelPago || coin != lastTelCoin ||
        premios != lastTelPremios || banco != lastTelBanco ||
        coins != lastTelCoins) {
        return false;
    }
    return (millis() - lastTelemetriaEnvio) < TELEMETRIA_DEDUP_MS;
}

void marcarTelemetriaEnviada(int pago, int coin, int premios, int banco, int coins) {
    lastTelPago = pago;
    lastTelCoin = coin;
    lastTelPremios = premios;
    lastTelBanco = banco;
    lastTelCoins = coins;
    lastTelemetriaEnvio = millis();
}

void connectToWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    // disconnect(false) no borra credenciales; true a veces deja el STA colgado
    WiFi.disconnect(false);
    delay(200);

    Serial.println("Conectando a WiFi...");
    WiFi.begin(ssid, password);
    lastWifiBeginAttempt = millis();

    // ~20 intentos x 500 ms ≈ 10 s (como antes)
    int retry = 0;
    const int maxRetry = 20;
    while (WiFi.status() != WL_CONNECTED && retry < maxRetry) {
        delay(500);
        Serial.print(".");
        retry++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Conectado");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("RSSI: ");
        Serial.println(WiFi.RSSI());
    } else {
        Serial.println("\nWiFi no conecto en setup; auto-reconnect + retry en loop");
    }
}

void mantenerRed() {
    if (debeEnviarHeartbeat) {
        enviarHeartbeat();
        debeEnviarHeartbeat = false;
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (!client.connected()) reconnectMQTT();
        client.loop();
        return;
    }

    // Un solo camino de reconexion:
    // 1) setAutoReconnect(true) trabaja solo
    // 2) solo si sigue caido > WIFI_RETRY_MS: disconnect + un begin limpio
    // Nunca spamear begin mientras el STA ya esta "connecting"
    unsigned long ahora = millis();
    if (ahora - lastWifiBeginAttempt < WIFI_RETRY_MS) return;
    lastWifiBeginAttempt = ahora;

    Serial.print("WiFi caido (status=");
    Serial.print((int)WiFi.status());
    Serial.println("): reconexion limpia");

    WiFi.disconnect(false); // corta intento en curso (evita "sta is connecting")
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);
}

void delayConRed(unsigned long ms) {
    unsigned long inicio = millis();
    while (millis() - inicio < ms) {
        leecoin(); // monedero antes que red (red puede bloquear)
        mantenerRed();
        delay(10);
    }
}


void reconnectMQTT() {
    // Solo intentamos reconectar cada 5 segundos para no bloquear la máquina
    if (millis() - lastMqttReconnectAttempt > 5000) {
        lastMqttReconnectAttempt = millis();
        
        Serial.print("Intentando reconexión MQTT...");
        // Intentar conectar (esto es rápido y no bloquea)
        if (client.connect(CODIGO_HARDWARE, "maquinas/status", 1, true, "offline")) {
            Serial.println("¡Conectado!");
            client.publish("maquinas/status", "online");
            enviarTelemetriaSiNuevaPartida();
        } else {
            Serial.print("Fallo rc="); 
            Serial.println(client.state());
        }
    }
}

bool enviarTelemetriaGrua() {
    if (!client.connected()) return false;

    int pago = PAGO;
    int coin = PJFIJO;       // partidas / juegos jugados
    int premios = PPFIJO;
    int banco = BANK;
    int coins = FICHAS_TOTAL; // fichas ingresadas (total acumulado)

    if (telemetriaEsDuplicada(pago, coin, premios, banco, coins)) return true;

    JsonDocument doc;
    doc["action"] = 2;
    doc["dni_admin"] = DNI_ADMIN;
    doc["codigo_hardware"] = CODIGO_HARDWARE;
    doc["tipo_maquina"] = TIPO_MAQUINA;
    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["pago"] = pago;
    payload["coin"] = coin;
    payload["premios"] = premios;
    payload["banco"] = banco;
    payload["coins"] = coins;

    char buffer[320];
    serializeJson(doc, buffer);
    if (client.publish(topic_datos, buffer)) {
        marcarTelemetriaEnviada(pago, coin, premios, banco, coins);
        Serial.println("Telemetria MQTT enviada");
        return true;
    }
    return false;
}

bool enviarTelemetriaSiNuevaPartida() {
    if (PJFIJO <= lastTelCoin && lastTelCoin >= 0) return false;
    return enviarTelemetriaGrua();
}

void activarHeartbeat() { debeEnviarHeartbeat = true; }

bool enviarHeartbeat() {
    if (!client.connected()) return false;

    JsonDocument doc;
    doc["action"] = 1;
    doc["dni_admin"] = DNI_ADMIN;
    doc["codigo_hardware"] = CODIGO_HARDWARE;
    doc["tipo_maquina"] = TIPO_MAQUINA;
    doc["ts"] = millis();

    char buffer[160];
    serializeJson(doc, buffer);
    if (client.publish(topic_heartbeat, buffer)) {
        Serial.println("Heartbeat enviado");
        return true;
    }
    return false;
}

// ============================================================================
// FUNCIONES DE JUEGO (Visualización)
// ============================================================================

void graficar() {
    if (GRUADISPLAY == 0) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("PJ:"); lcd.setCursor(3, 0); lcd.print(COIN);
        lcd.setCursor(9, 0); lcd.print("PP:"); lcd.setCursor(12, 0); lcd.print(CONTSALIDA);
        lcd.setCursor(0, 1); lcd.print("PA:"); lcd.setCursor(3, 1); lcd.print(PAGO);
        lcd.setCursor(7, 1); lcd.print(BANKTIEMPO); lcd.setCursor(9, 1); lcd.print("BK:");
        lcd.setCursor(12, 1); lcd.print(BANK);
    }
    if (GRUADISPLAY == 1) {
        lcd.clear();
        if (MODO_MAQUINA == MODO_CONTROL) {
            lcd.setCursor(0, 0); lcd.print("Credito:");
            lcd.setCursor(9, 0); lcd.print(CREDITO);
        } else {
            lcd.setCursor(0, 0); lcd.print("PJ:");
            lcd.setCursor(3, 0); lcd.print(PJFIJO);
        }
        lcd.setCursor(0, 1); lcd.print("Fichas:");
        lcd.setCursor(8, 1); lcd.print(monedasAcumuladas);
        lcd.print("/");
        lcd.print(FICHAS_POR_JUEGO < 1 ? 1 : FICHAS_POR_JUEGO);
    }
}

void actualizarPantalla() {
    if (!pantallaSucia) return;
    pantallaSucia = false;
    graficar();
}

void sincronizarPrevContadores() {
    prevPJFIJO = PJFIJO;
    prevPPFIJO = PPFIJO;
    prevBANK = BANK;
    prevPAGO = PAGO;
    prevFICHAS_TOTAL = FICHAS_TOTAL;
}

// Solo actualiza contadores/RAM/EEPROM-put. Flush y MQTT los hace leecoin() al final del lote.
void registrarPartidoPorMoneda() {
    if (MODO_MAQUINA == MODO_CONTROL) {
        CREDITO++;
        sincronizarPrevContadores();
        marcarPantalla();
        return;
    }

    COIN++;
    PJFIJO++;
    BANK++;
    premioCoinPendiente = true;
    infrarrojoDetectando = false;
    infrarrojoDesde = 0;
    // Si el infrarrojo ya esta tapado, espera que quede libre antes de armarse.
    BARRERAAUX = (BARRERAAUX2 == 0 && digitalRead(DATO7) == HIGH) ? HIGH : LOW;
    BARRERA = LOW;
    putEEPROMUint(1, COIN);
    putEEPROMUint(25, PJFIJO);
    putEEPROM(9, BANK);
    sincronizarPrevContadores();
    marcarPantalla();
}

void tareasIdle() {
    leecoin();

    if (PJFIJO != prevPJFIJO || PPFIJO != prevPPFIJO || BANK != prevBANK ||
        PAGO != prevPAGO || FICHAS_TOTAL != prevFICHAS_TOTAL) {
        sincronizarPrevContadores();
        marcarPantalla();
    }

    if (digitalRead(DATO3) == LOW) AUXDATO3++;
    else AUXDATO3 = 0;

    if (digitalRead(DATO3) == LOW && AUXDATO3 == 30) {
        tickerPulso.detach();
        programar();
        tickerPulso.attach(60, activarHeartbeat);
        AUXDATO3 = 0;
    }

    CTIEMPO++;
    if (CTIEMPO >= 18000 && BANKTIEMPO > 0) {
        CTIEMPO = 0; BANKTIEMPO--;
        marcarPantalla();
    }

    if (TIEMPO8 < 200) {
        TIEMPO8++;
        if (TIEMPO8 == 199) {
            TIEMPO8 = 0;
            rearmarBarreraSiLibre();
            if (BARRERAAUX == LOW) leerbarrera();
        }
    }
}

// ISR: cuenta en LOW solo si venia HIGH (coinArmed). Re-arma en HIGH.
// Permite 2 fichas rapidas si el pulso vuelve a HIGH entre medias; filtra rebote.
void IRAM_ATTR coinIsr() {
    unsigned long ahora = micros();
    if ((ahora - lastCoinEdgeUs) < COIN_DEBOUNCE_US) return;
    lastCoinEdgeUs = ahora;

    if (digitalRead(ECOIN) == LOW) {
        if (!coinArmed) return;
        coinArmed = false;
        coinPulsesPendientes++;
    } else {
        coinArmed = true;
    }
}

// Drena pulsos de la ISR y aplica contadores. Un solo flush EEPROM + telemetria al final.
void leecoin() {
    noInterrupts();
    uint32_t n = coinPulsesPendientes;
    coinPulsesPendientes = 0;
    interrupts();
    if (n == 0) return;

    int fichasPorJuego = FICHAS_POR_JUEGO < 1 ? 1 : FICHAS_POR_JUEGO;
    unsigned int pjAntes = PJFIJO;

    for (uint32_t i = 0; i < n; i++) {
        FICHAS_TOTAL++;
        putEEPROMUint(EEPROM_FICHAS_TOTAL, FICHAS_TOTAL);
        monedasAcumuladas++;
        while (monedasAcumuladas >= fichasPorJuego) {
            monedasAcumuladas -= fichasPorJuego;
            registrarPartidoPorMoneda();
        }
    }

    flushEEPROM();
    if (PJFIJO > pjAntes) {
        enviarTelemetriaSiNuevaPartida();
    }
}

void rearmarBarreraSiLibre() {
    // COIN + infrarrojo: al encender queda desarmada hasta el primer juego.
    // Despues de un premio, si la rampa vuelve a quedar libre, se rearma
    // (permite destapar un peluche trabado y tirarlo por la barrera).
    if (MODO_MAQUINA == MODO_COIN && BARRERAAUX2 == 0 && !premioCoinPendiente) {
        BARRERAAUX = HIGH;
        return;
    }

    if (BARRERAAUX != HIGH) return;

    if (BARRERAAUX2 == 0) {
        if (digitalRead(DATO7) == LOW) {
            infrarrojoDetectando = false;
            infrarrojoDesde = 0;
            BARRERAAUX = LOW;
        }
        return;
    }

    digitalWrite(triger, LOW); delayMicroseconds(2);
    digitalWrite(triger, HIGH); delayMicroseconds(10);
    tiempo = pulseIn(echo, HIGH, 30000);
    distancia = tiempo / 10;
    if (!((distancia + 130) < RDISTANCIA || (distancia - 130) > RDISTANCIA)) {
        BARRERAAUX = LOW;
    }
}

void leerbarrera() {
    if (BARRERAAUX2 == 1) {
        digitalWrite(triger, LOW); delayMicroseconds(2);
        digitalWrite(triger, HIGH); delayMicroseconds(10);
        tiempo = pulseIn(echo, HIGH, 30000);
        distancia = tiempo / 10;
        if ((distancia + 130) < RDISTANCIA || (distancia - 130) > RDISTANCIA) BARRERA = HIGH;
        if (BARRERA == HIGH) {
            lcd.setCursor(0, 1); lcd.print("###");
            BARRERA = LOW;
            if (digitalRead(DATO12) == HIGH) {
                CONTSALIDA++; PPFIJO++; BANK -= PAGO;
                BARRERAAUX = HIGH;
                putEEPROMUint(5, CONTSALIDA);
                putEEPROMUint(29, PPFIJO);
                putEEPROM(9, BANK);
            }
            sincronizarPrevContadores();
            marcarPantalla();
            flushEEPROM();
        }
    }
    if (BARRERAAUX2 == 0) {
        // Confirma el bloqueo durante 50 ms para rechazar ruido al encender
        // e interferencias breves del sensor.
        if (digitalRead(DATO7) == LOW) {
            infrarrojoDetectando = false;
            infrarrojoDesde = 0;
            BARRERA = LOW;
            return;
        }
        if (!infrarrojoDetectando) {
            infrarrojoDetectando = true;
            infrarrojoDesde = millis();
            return;
        }
        if (millis() - infrarrojoDesde < INFRARROJO_CONFIRMACION_MS) return;
        BARRERA = HIGH;
        if (BARRERA == HIGH) {
            lcd.setCursor(0, 1); lcd.print("###");
            CONTSALIDA++; PPFIJO++; BANK -= PAGO;
            BARRERA = LOW;
            BARRERAAUX = HIGH; // desarmar; se rearmara cuando DATO7 quede libre
            infrarrojoDetectando = false;
            infrarrojoDesde = 0;
            putEEPROMUint(5, CONTSALIDA);
            putEEPROMUint(29, PPFIJO);
            putEEPROM(9, BANK);
            sincronizarPrevContadores();
            marcarPantalla();
            flushEEPROM();
        }
    }
}

// ============================================================================
// PINZA (solo MODO CONTROL)
// ============================================================================

void delayConMonedas(unsigned long ms) {
    unsigned long inicio = millis();
    while (millis() - inicio < ms) {
        leecoin();
        mantenerRed();
        delay(1);
    }
}

void jugarPinza() {
    int auxtbarrera = 0;
    unsigned long inicioJuego = millis();
    const unsigned long TIMEOUT_JUEGO_MS = 60000;

    BARRERAAUX = LOW; AUX = 0; BARRERA = LOW;
    if (CREDITO > 0) CREDITO--;

    COIN++; BANK++; PJFIJO++;
    if (BANKTIEMPO < 10) BANKTIEMPO++;

    putEEPROM(9, BANK);
    putEEPROMUint(25, PJFIJO);
    putEEPROMUint(1, COIN);
    sincronizarPrevContadores();
    marcarPantalla();
    actualizarPantalla();
    flushEEPROM();
    enviarTelemetriaSiNuevaPartida();

    Z = random(5);

    // PINZA CON PREMIO
    if (PAGO <= BANK && Z <= 3) {
        analogWrite(SPINZA, 250);
        delayConMonedas(2000);
        auxtbarrera = HIGH;
        while (auxtbarrera == HIGH) {
            while (X < 3000) {
                leecoin();
                mantenerRed();
                if (digitalRead(EPINZA) == HIGH) X = 0;
                if (X == 150) analogWrite(SPINZA, 0);
                if (digitalRead(EPINZA) == LOW) { X++; delay(1); }

                if (TIEMPO8 <= 20) TIEMPO8++;
                if (TIEMPO8 >= 19) {
                    TIEMPO8 = 0;
                    if (BARRERAAUX == LOW) leerbarrera();
                }
                if (X == 2998) auxtbarrera = LOW;
                if (BARRERAAUX == HIGH) { auxtbarrera = LOW; break; }
                if (millis() - inicioJuego > TIMEOUT_JUEGO_MS) { auxtbarrera = LOW; break; }
            }
            if (X >= 3000) auxtbarrera = LOW;
        }
    }
    // PINZA SIN PREMIO
    else {
        analogWrite(SPINZA, 255);
        delayConMonedas(TIEMPO5);
        FUERZAAUX2 = FUERZA;
        if (BANK <= -10) FUERZAAUX2 = FUERZA * 0.8;

        FUERZAV = random(FUERZAAUX2 * 1.8, FUERZAAUX2 * 2.5);
        FUERZAAUX = (FUERZAV - FUERZAAUX2) / 10;
        TIEMPOAUX = 0; TIEMPOAUX1 = (TIEMPO / 10);

        while (TIEMPOAUX <= 10) {
            FUERZAV -= FUERZAAUX;
            analogWrite(SPINZA, FUERZAV);
            TIEMPOAUX++;
            delayConMonedas(TIEMPOAUX1);
        }

        analogWrite(SPINZA, FUERZA * 1.3);
        delayConMonedas(300);
        analogWrite(SPINZA, FUERZA);
        delayConMonedas(100);
        analogWrite(SPINZA, FUERZA * 1.3);
        auxtbarrera = HIGH;

        while (auxtbarrera == HIGH) {
            while (X < 3000) {
                leecoin();
                mantenerRed();

                if (digitalRead(EPINZA) == HIGH) X = 0;
                if (X == 150) analogWrite(SPINZA, 0);
                if (digitalRead(EPINZA) == LOW) { X++; delay(1); }

                if (TIEMPO8 <= 20) TIEMPO8++;
                if (TIEMPO8 >= 19) {
                    TIEMPO8 = 0;
                    if (BARRERAAUX == LOW) leerbarrera();
                }
                if (X == 2998) auxtbarrera = LOW;
                if (BARRERAAUX == HIGH) { auxtbarrera = LOW; break; }
                if (millis() - inicioJuego > TIMEOUT_JUEGO_MS) { auxtbarrera = LOW; break; }
            }
            if (X >= 3000) auxtbarrera = LOW;
        }
    }

    analogWrite(SPINZA, 0);
    TIEMPO8 = 0; X = 0; A = 0;
    marcarPantalla();
}

void atenderPinza() {
    unsigned long ahora = millis();
    if (ahora - lastPinzaTick < 1) return;
    lastPinzaTick = ahora;

    if (digitalRead(EPINZA) == HIGH) {
        if (AUX < 5) AUX++;
    } else {
        // Switch cerrado (pinza en reposo): rearmar para el proximo juego
        if (AUX > 0) AUX--;
        if (AUX == 0) epinzaArmado = true;
        return;
    }

    if (AUX < 5) return;
    AUX = 0;

    // Solo cuenta si venia de reposo: 1 apertura = 1 juego, siempre,
    // sin importar la cantidad de fichas ni el credito disponible
    if (!epinzaArmado) return;
    epinzaArmado = false;

    jugarPinza();
}

void ajustebarrera() {
    if (BARRERAAUX2 == 1) {
        digitalWrite(triger, LOW); delayMicroseconds(2);
        digitalWrite(triger, HIGH); delayMicroseconds(10);
        tiempo = pulseIn(echo, HIGH);
        distancia = tiempo / 10;
        if ((distancia + 130) < RDISTANCIA || (distancia - 130) > RDISTANCIA) BARRERA = HIGH;
        if (BARRERA == HIGH) { lcd.setCursor(0, 1); lcd.print("###"); delayConRed(1000); }
    }
    if (BARRERAAUX2 == 0) {
        if (digitalRead(DATO7) == HIGH) BARRERA = HIGH;
        if (BARRERA == HIGH) { AUXSIM = HIGH; lcd.setCursor(0, 1); lcd.print("###"); delayConRed(1000); }
    }
}

bool leerBotonConDebounce(int pin, int delayMs = 50) {
    if (digitalRead(pin) == LOW) {
        delayConRed(delayMs);
        if (digitalRead(pin) == LOW) {
            while (digitalRead(pin) == LOW) {
                delay(10);
                mantenerRed();
            }
            delayConRed(50);
            return true;
        }
    }
    return false;
}

// Mantener apretado: 1er toque = 1 paso; luego acelera (y multiplica pasos).
// Retorna cantidad de pasos a aplicar (0 = nada).
int leerBotonHold(int pin) {
    static int pinActivo = -1;
    static unsigned long tInicio = 0;
    static unsigned long tUltimo = 0;

    if (digitalRead(pin) != LOW) {
        if (pinActivo == pin) pinActivo = -1;
        return 0;
    }

    unsigned long ahora = millis();

    if (pinActivo != pin) {
        delay(25);
        if (digitalRead(pin) != LOW) return 0;
        pinActivo = pin;
        tInicio = ahora;
        tUltimo = ahora;
        return 1;
    }

    unsigned long held = ahora - tInicio;
    if (held < 350) return 0; // espera antes de repetir

    unsigned long intervalo;
    int pasos;
    if (held < 1200) {
        intervalo = 140;
        pasos = 1;
    } else if (held < 2500) {
        intervalo = 60;
        pasos = 1;
    } else if (held < 4500) {
        intervalo = 35;
        pasos = 2;
    } else {
        intervalo = 25;
        pasos = 5;
    }

    if (ahora - tUltimo >= intervalo) {
        tUltimo = ahora;
        return pasos;
    }
    return 0;
}

void programar() {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("VERSION 1.8");
    lcd.setCursor(0,1);
    lcd.print("11/10/26");
    delayConRed(500);
    
    // Esperar a que se suelte DATO3 si está presionado
    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);  // Delay adicional
    
    // Esperar a que se presione DATO3
    while (digitalRead(DATO3) == HIGH) {
        delay(20);
        mantenerRed();
    }
    // Esperar a que se suelte
    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);

    // Pantalla PJ/PP
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("PJ:");
    lcd.setCursor(4,0);
    lcd.print(PJFIJO);
    lcd.setCursor(0,1);
    lcd.print("PP:");
    lcd.setCursor(4,1);
    lcd.print(PPFIJO);
    delayConRed(500);
    
    while (digitalRead(DATO3) == HIGH) {
        delay(20);
        mantenerRed();
    }
    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);
    
    GRUADISPLAY = 0;
    graficar();
    delayConRed(500);
    
    while (digitalRead(DATO3) == HIGH) {
        delay(20);
        mantenerRed();
    }
    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);
    
    GRUADISPLAY = getEEPROM(37);

    // BORRA CONTADORES con debounce mejorado
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("BORRA CONTADORES");
    lcd.setCursor(0,1);
    lcd.print("NO");
    delayConRed(500);
    
    while (digitalRead(DATO3) == HIGH) {
        if (leerBotonConDebounce(DATO6)) {
            BORRARCONTADORES = HIGH;
            lcd.setCursor(0,1);
            lcd.print("SI");
        }
        if (leerBotonConDebounce(DATO10)) {
            BORRARCONTADORES = LOW;
            lcd.setCursor(0,1);
            lcd.print("NO");
        }
        delay(10);
        mantenerRed();
    }
    
    // Esperar a que se suelte DATO3
    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);
    
    if (BORRARCONTADORES == HIGH) {
        putEEPROMUint(1, 0);
        putEEPROMUint(5, 0);
        putEEPROM(9, 0);
        putEEPROMUint(EEPROM_FICHAS_TOTAL, 0);
        COIN = 0;
        CONTSALIDA = 0;
        BANK = 0;
        FICHAS_TOTAL = 0;
        BORRARCONTADORES = LOW;
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("BORRADOS");
        delayConRed(1000);
    }

    // Display Modo
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Display Modo");
    if(GRUADISPLAY == 0) {
        lcd.setCursor(0,1);
        lcd.print("Modo Contadores");
    }
    if(GRUADISPLAY == 1) {
        lcd.setCursor(0,1);
        lcd.print("Modo Coin");
    }
    delayConRed(500);
    
    while (digitalRead(DATO3) == HIGH) {
        if (leerBotonConDebounce(DATO6)) {
            GRUADISPLAY = 0;
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("Display Modo");
            lcd.setCursor(0,1);
            lcd.print("Modo Contadores");
        }
        if (leerBotonConDebounce(DATO10)) {
            GRUADISPLAY = 1;
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("Display Modo");
            lcd.setCursor(0,1);
            lcd.print("Modo Coin");
        }
        delay(10);
        mantenerRed();
    }
    
    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);
    
    putEEPROM(37, GRUADISPLAY);

    // AJUSTAR PAGO
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("AJUSTAR PAGO");
    delayConRed(500);
    
    while (digitalRead(DATO3) == HIGH) {
        lcd.setCursor(0,1);
        lcd.print("   ");  // Limpiar
        lcd.setCursor(0,1);
        lcd.print(PAGO);

        int up = leerBotonHold(DATO6);
        int down = leerBotonHold(DATO10);
        if (up > 0) PAGO += up;
        if (down > 0) PAGO -= down;
        delay(10);
        mantenerRed();
    }
    
    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);
    
    putEEPROM(13, PAGO);

    // FICHAS POR JUEGO
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("FICHAS/JUEGO");
    delayConRed(500);

    while (digitalRead(DATO3) == HIGH) {
        lcd.setCursor(0, 1);
        lcd.print("   ");
        lcd.setCursor(0, 1);
        lcd.print(FICHAS_POR_JUEGO);

        int up = leerBotonHold(DATO6);
        int down = leerBotonHold(DATO10);
        if (up > 0 && FICHAS_POR_JUEGO < 10) {
            FICHAS_POR_JUEGO += up;
            if (FICHAS_POR_JUEGO > 10) FICHAS_POR_JUEGO = 10;
        }
        if (down > 0 && FICHAS_POR_JUEGO > 1) {
            FICHAS_POR_JUEGO -= down;
            if (FICHAS_POR_JUEGO < 1) FICHAS_POR_JUEGO = 1;
        }
        delay(10);
        mantenerRed();
    }

    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);

    putEEPROM(EEPROM_FICHAS_POR_JUEGO, FICHAS_POR_JUEGO);

    // MODO MAQUINA
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("MODO MAQUINA");
    lcd.setCursor(0,1);
    if (MODO_MAQUINA == MODO_CONTROL) lcd.print("CONTROL");
    else lcd.print("COIN");
    delayConRed(500);

    while (digitalRead(DATO3) == HIGH) {
        if (leerBotonConDebounce(DATO6)) {
            MODO_MAQUINA = MODO_COIN;
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("MODO MAQUINA");
            lcd.setCursor(0,1);
            lcd.print("COIN");
        }
        if (leerBotonConDebounce(DATO10)) {
            MODO_MAQUINA = MODO_CONTROL;
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("MODO MAQUINA");
            lcd.setCursor(0,1);
            lcd.print("CONTROL");
        }
        delay(10);
        mantenerRed();
    }

    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);

    putEEPROM(EEPROM_MODO_MAQUINA, MODO_MAQUINA);

    // ID DISPOSITIVO
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ID DISPOSITIVO");
    delayConRed(500);

    while (digitalRead(DATO3) == HIGH) {
        lcd.setCursor(0, 1);
        lcd.print("     ");
        lcd.setCursor(0, 1);
        lcd.print(ID_DISPOSITIVO);

        int up = leerBotonHold(DATO6);
        int down = leerBotonHold(DATO10);
        if (up > 0 && ID_DISPOSITIVO < ID_DISPOSITIVO_MAX) {
            ID_DISPOSITIVO += up;
            if (ID_DISPOSITIVO > ID_DISPOSITIVO_MAX) ID_DISPOSITIVO = ID_DISPOSITIVO_MAX;
        }
        if (down > 0 && ID_DISPOSITIVO > ID_DISPOSITIVO_MIN) {
            ID_DISPOSITIVO -= down;
            if (ID_DISPOSITIVO < ID_DISPOSITIVO_MIN) ID_DISPOSITIVO = ID_DISPOSITIVO_MIN;
        }
        delay(10);
        mantenerRed();
    }

    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);

    putEEPROM(EEPROM_ID_DISPOSITIVO, ID_DISPOSITIVO);
    flushEEPROM();
    aplicarIdDispositivo();
    if (client.connected()) client.disconnect();

    // AJUSTAR TIEMPO
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("AJUSTAR TIEMPO");
    delayConRed(500);
    
    while (digitalRead(DATO3) == HIGH) {
        lcd.setCursor(0,1);
        lcd.print("     ");  // Limpiar
        lcd.setCursor(0,1);
        lcd.print(TIEMPO);

        int up = leerBotonHold(DATO6);
        int down = leerBotonHold(DATO10);
        if (up > 0 && TIEMPO < 5000) {
            TIEMPO += 10 * up;
            if (TIEMPO > 5000) TIEMPO = 5000;
        }
        if (down > 0 && TIEMPO > 500) {
            TIEMPO -= 10 * down;
            if (TIEMPO < 500) TIEMPO = 500;
        }
        delay(10);
        mantenerRed();
    }
    
    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);
    
    putEEPROM(17, TIEMPO);

    // TIEMPO F. FUERTE
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("TIEMPO F. FUERTE");
    delayConRed(500);
    
    while (digitalRead(DATO3) == HIGH) {
        lcd.setCursor(0,1);
        lcd.print("     ");  // Limpiar
        lcd.setCursor(0,1);
        lcd.print(TIEMPO5);

        int up = leerBotonHold(DATO6);
        int down = leerBotonHold(DATO10);
        if (up > 0 && TIEMPO5 < 5000) {
            TIEMPO5 += 10 * up;
            if (TIEMPO5 > 5000) TIEMPO5 = 5000;
        }
        if (down > 0 && TIEMPO5 > 0) {
            TIEMPO5 -= 10 * down;
            if (TIEMPO5 < 0) TIEMPO5 = 0;
        }
        delay(10);
        mantenerRed();
    }
    
    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);
    
    putEEPROM(41, TIEMPO5);

    // AJUSTAR FUERZA
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("AJUSTAR FUERZA");
    delayConRed(500);
    
    while (digitalRead(DATO3) == HIGH) {
        lcd.setCursor(0,1);
        lcd.print("   ");  // Limpiar
        lcd.setCursor(0,1);
        lcd.print(FUERZA);

        int up = leerBotonHold(DATO6);
        int down = leerBotonHold(DATO10);
        if (up > 0 && FUERZA < 101) {
            FUERZA += up;
            if (FUERZA > 101) FUERZA = 101;
        }
        if (down > 0 && FUERZA > 5) {
            FUERZA -= down;
            if (FUERZA < 5) FUERZA = 5;
        }
        delay(10);
        mantenerRed();
    }
    
    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);
    
    putEEPROM(21, FUERZA);

    // TIPO BARRERA
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("TIPO BARRERA");
    if(BARRERAAUX2 == 0) {
        lcd.setCursor(0,1);
        lcd.print("INFRARROJO");
    }
    if(BARRERAAUX2 == 1) {
        lcd.setCursor(0,1);
        lcd.print("ULTRASONIDO");
    }
    delayConRed(500);
    
    while (digitalRead(DATO3) == HIGH) {
        if (leerBotonConDebounce(DATO6)) {
            BARRERAAUX2 = 0;
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("TIPO BARRERA");
            lcd.setCursor(0,1);
            lcd.print("INFRARROJO");
        }
        if (leerBotonConDebounce(DATO10)) {
            BARRERAAUX2 = 1;
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("TIPO BARRERA");
            lcd.setCursor(0,1);
            lcd.print("ULTRASONIDO");
        }
        delay(10);
        mantenerRed();
    }
    
    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);
    
    putEEPROM(33, BARRERAAUX2);

    // PRUEBA BARRERA
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("PRUEBA BARRERA");
    delayConRed(500);
    
    while (digitalRead(DATO3) == HIGH) {
        lcd.setCursor(0,1);
        lcd.print("        ");  // Limpiar
        lcd.setCursor(0,1);
        lcd.print(distancia);
        lcd.setCursor(8,1);
        lcd.print(RDISTANCIA);
        ajustebarrera();
        
        if (BARRERA == HIGH) {
            lcd.setCursor(0,1);
            lcd.print("###");
            delayConRed(1000);
            BARRERA = LOW;
        }
        delay(50);
    }
    
    while (digitalRead(DATO3) == LOW) {
        delay(20);
        mantenerRed();
    }
    delayConRed(100);

    graficar();
    delayConRed(500);
    marcarPantalla();
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    
    // INICIALIZACIÓN DE MEMORIA SEGÚN EL MODO
    if (USAR_EEPROM_EXTERNA == 1) {
        Wire.begin(); // I2C Hardware
        Serial.println("MODO: EEPROM EXTERNA (I2C)");
    } else {
        EEPROM.begin(EEPROM_SIZE_INTERNAL); // Emulación Flash
        Wire.begin(); // Iniciamos I2C igual para el LCD
        Serial.println("MODO: EEPROM INTERNA (FLASH)");
    }

    WiFi.setSleep(false); // Evita que el ESP32 apague la antena para ahorrar batería
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setAutoReconnect(true); // El chip ESP32 intentará reconectarse solo al router en segundo plano

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    analogWriteFrequency(SPINZA, PWM_FREQUENCY);
#else
    analogWriteFrequency(PWM_FREQUENCY); // ESP32 core 2.x: frecuencia global de analogWrite
#endif
    lcd.init(); lcd.backlight();

    pinMode(triger, OUTPUT); pinMode(echo, INPUT);
    pinMode(DATO7, INPUT_PULLUP); pinMode(DATO5, OUTPUT);
    pinMode(DATO3, INPUT_PULLUP); pinMode(SPINZA, OUTPUT);
    pinMode(EPINZA, INPUT_PULLUP); pinMode(DATO11, OUTPUT);
    pinMode(DATO6, INPUT_PULLUP); pinMode(DATO10, INPUT_PULLUP);
    pinMode(DATO12, INPUT_PULLUP);
#if defined(ESP32)
    if (ECOIN == 25 || ECOIN == 26) dacDisable(ECOIN);
#endif
    pinMode(ECOIN, INPUT_PULLUP);
    coinPulsesPendientes = 0;
    coinArmed = (digitalRead(ECOIN) == HIGH);
    attachInterrupt(digitalPinToInterrupt(ECOIN), coinIsr, CHANGE);

    // Leer EEPROM (Automático según modo)
    INICIO = getEEPROM(45);
    if (INICIO != 35) {
        Serial.println("Inicializando memoria por primera vez...");
        putEEPROMUint(1, 0); putEEPROMUint(5, 0); putEEPROM(9, 0);
        putEEPROM(13, 12); putEEPROM(17, 2000); putEEPROM(21, 50);
        putEEPROMUint(25, 0); putEEPROMUint(29, 0); putEEPROM(33, 0);
        putEEPROM(37, 0); putEEPROM(41, 0); putEEPROM(45, 35);
        putEEPROM(EEPROM_FICHAS_POR_JUEGO, 1);
        putEEPROM(EEPROM_MODO_MAQUINA, MODO_COIN);
        putEEPROMUint(EEPROM_FICHAS_TOTAL, 0);
        putEEPROM(EEPROM_ID_DISPOSITIVO, 1);
    }

    COIN = getEEPROMUint(1); CONTSALIDA = getEEPROMUint(5);
    BANK = getEEPROM(9); PAGO = getEEPROM(13);
    TIEMPO = getEEPROM(17); FUERZA = getEEPROM(21);
    PJFIJO = getEEPROMUint(25); PPFIJO = getEEPROMUint(29);
    BARRERAAUX2 = getEEPROM(33); GRUADISPLAY = getEEPROM(37);
    TIEMPO5 = getEEPROM(41);
    FICHAS_TOTAL = getEEPROMUint(EEPROM_FICHAS_TOTAL);
    FICHAS_POR_JUEGO = getEEPROM(EEPROM_FICHAS_POR_JUEGO);
    if (FICHAS_POR_JUEGO < 1) {
        FICHAS_POR_JUEGO = 1;
        putEEPROM(EEPROM_FICHAS_POR_JUEGO, FICHAS_POR_JUEGO);
    }
    MODO_MAQUINA = getEEPROM(EEPROM_MODO_MAQUINA);
    if (MODO_MAQUINA != MODO_CONTROL) MODO_MAQUINA = MODO_COIN;
    ID_DISPOSITIVO = getEEPROM(EEPROM_ID_DISPOSITIVO);
    if (ID_DISPOSITIVO < ID_DISPOSITIVO_MIN || ID_DISPOSITIVO > ID_DISPOSITIVO_MAX) {
        ID_DISPOSITIVO = ID_DISPOSITIVO_MIN;
        putEEPROM(EEPROM_ID_DISPOSITIVO, ID_DISPOSITIVO);
    }
    aplicarIdDispositivo();
    // COIN + infrarrojo: no contar nada hasta que se registre el primer juego.
    if (MODO_MAQUINA == MODO_COIN && BARRERAAUX2 == 0) {
        premioCoinPendiente = false;
        BARRERAAUX = HIGH;
        BARRERA = LOW;
    }

    prevPJFIJO = PJFIJO;
    prevPPFIJO = PPFIJO;
    prevBANK = BANK;
    prevPAGO = PAGO;
    prevFICHAS_TOTAL = FICHAS_TOTAL;

    if (digitalRead(DATO3) == LOW){ 
        lcd.clear(); lcd.setCursor(0,0); lcd.print("CONT.FIJOS"); 
        lcd.setCursor(0,1); lcd.print("PJ:"); lcd.print(PJFIJO); 
        lcd.print(" PP:"); lcd.print(PPFIJO); delayConRed(1000);
        while (digitalRead(DATO3) == HIGH) delay(20);
    }
    delayConRed(1000);
    
    Y = 2;
    while(B <= 10){ 
        digitalWrite(triger,LOW); delayMicroseconds(2);
        digitalWrite(triger,HIGH); delayMicroseconds(10);
        tiempo = pulseIn(echo,HIGH); distancia = tiempo/10;
        RDISTANCIA = distancia; B++; delay(50);
    }
    
    digitalWrite(DATO11, 1);
    
    // CONEXIÓN MQTT
    connectToWiFi();
    client.setServer(mqtt_server, mqtt_port);
    client.setBufferSize(512);
    client.setKeepAlive(60);
    client.setSocketTimeout(15);
    tickerPulso.attach(60, activarHeartbeat);
    marcarPantalla();
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================

void loop() {
    leecoin(); // primero: pulsos ISR no dependen de red/MQTT
    mantenerRed();
    actualizarPantalla();
    tareasIdle();

    if (MODO_MAQUINA == MODO_CONTROL) atenderPinza();
}
