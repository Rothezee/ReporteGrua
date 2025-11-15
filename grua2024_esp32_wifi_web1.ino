// Gold Digger - Versión MQTT Optimizada - HEARTBEAT CORREGIDO
// Noviembre 2025

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <Ticker.h>

// ============================================================================
// CONFIGURACIÓN Y CONSTANTES
// ============================================================================

// --- PINES ---
#define PIN_TRIGER 13
#define PIN_ECHO 12
#define PIN_DATO11 19
#define PIN_DATO7 14
#define PIN_DATO3 4
#define PIN_DATO5 25
#define PIN_PINZA_ENABLE 17
#define PIN_PINZA_PWM 16
#define PIN_DATO6 34
#define PIN_DATO10 35
#define PIN_DATO12 27
#define PIN_COIN 26

// Constantes
#define EEPROM_INIT_CHECK_VALUE 35
#define EEPROM_SIZE 512
#define PWM_FREQUENCY 100

// Configuración LCD
int lcdColumns = 16;
int lcdRows = 2;
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);

// --- CONFIGURACIÓN WIFI Y MQTT ---
const char* device_id = "ESP32_005";
const char* ssid = "MOVISTAR-WIFI6-0160";
const char* password = "46332714";
const char* mqtt_server = "broker.emqx.io";  // ✅ Broker actualizado
const int mqtt_port = 1883;

// --- CLIENTES WIFI Y MQTT ---
WiFiClient espClient;
PubSubClient client(espClient);

// --- TÓPICOS MQTT ---
const char* topic_datos = "maquinas/ESP32_005/datos";
const char* topic_estado = "maquinas/ESP32_005/estado";
const char* topic_heartbeat = "maquinas/ESP32_005/heartbeat";

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================

// Contadores principales
uint32_t COIN = 0;
uint32_t CONTSALIDA = 0;
int32_t BANK = 0;
uint32_t PJFIJO = 0;
uint32_t PPFIJO = 0;

// Configuración
int16_t PAGO = 20;
int16_t TIEMPO = 0;
int16_t FUERZA = 50;
int16_t TIEMPO5 = 0;
int16_t GRUADISPLAY = 0;
int16_t BARRERAAUX2 = 0;
int16_t INICIO = 0;

// Estado del juego
int CREDITO = 0;
unsigned int BANKTIEMPO = 0;
int BARRERA = 0;
int BARRERAAUX = 0;
int RDISTANCIA = 0;
int distancia = 0;
int tiempo = 0;

// Variables auxiliares
unsigned long X = 0;
unsigned int CTIEMPO = 0;
int AUX = 0;
int AUXCOIN = 0;
int AUX2COIN = 0;
int BORRARCONTADORES = LOW;
int AUXDATO3 = 0;
int TIEMPO7 = 0;
unsigned long TIEMPO8 = 0;
int AUXSIM = 0;
int Z = 0;
float FUERZAAUX = 0;
float FUERZAV = 0;
int TIEMPOAUX = 0;
int TIEMPOAUX1 = 0;
int FUERZAAUX2 = 0;
int B = 0;

// Variables para control de envío MQTT
uint32_t prevPJFIJO = 0;
uint32_t prevPPFIJO = 0;
int32_t prevBANK = 0;
unsigned long lastWifiCheck = 0;
const long wifiCheckInterval = 30000;

// ✅ Ticker para heartbeat Y FLAG
Ticker tickerPulso;
volatile bool debeEnviarHeartbeat = false;  // ✅ AGREGADO

// Variables para optimización de LCD
uint32_t prevCOIN = -1, prevCONTSALIDA = -1;
int32_t prevBANK_lcd = -1;
int16_t prevPAGO = -1;
unsigned int prevBANKTIEMPO = -1;
int prevCREDITO = -1;
int prevGRUADISPLAY = -1;

// ============================================================================
// FUNCIONES DE EEPROM
// ============================================================================

void putEEPROM(int address, int16_t value) {
    int16_t currentValue;
    EEPROM.get(address, currentValue);
    if (currentValue != value) {
        EEPROM.put(address, value);
        EEPROM.commit();
    }
}

void putEEPROM32(int address, uint32_t value) {
    uint32_t currentValue;
    EEPROM.get(address, currentValue);
    if (currentValue != value) {
        EEPROM.put(address, value);
        EEPROM.commit();
    }
}

void putEEPROM32_signed(int address, int32_t value) {
    int32_t currentValue;
    EEPROM.get(address, currentValue);
    if (currentValue != value) {
        EEPROM.put(address, value);
        EEPROM.commit();
    }
}

// ============================================================================
// FUNCIONES DE RED Y MQTT
// ============================================================================

void connectToWiFi() {
    WiFi.disconnect(true);
    delay(1000);

    Serial.println("Conectando a WiFi...");
    WiFi.begin(ssid, password);
    int retryCount = 0;
    while (WiFi.status() != WL_CONNECTED && retryCount < 20) {
        delay(1000);
        Serial.print(".");
        retryCount++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✓ WiFi conectado");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("✗ Error conectando WiFi");
    }
}

void reconnectMQTT() {
    if (!client.connected()) {
        Serial.print("Conectando MQTT...");
        if (client.connect(device_id, topic_estado, 1, true, "offline")) {
            Serial.println(" ✓ conectado!");
            client.publish(topic_estado, "online", true);
        } else {
            Serial.print(" ✗ falló, rc=");
            Serial.println(client.state());
        }
    }
}

void enviarDatosMQTT(uint32_t dato1, uint32_t dato2, uint32_t dato3, int32_t dato4) {
    if (!client.connected()) {
        return;
    }

    JsonDocument doc;
    doc["device_id"] = device_id;
    doc["pago"] = dato1;
    doc["partidas_jugadas"] = dato2;
    doc["premios_pagados"] = dato3;
    doc["banco"] = dato4;

    char buffer[256];
    size_t n = serializeJson(doc, buffer);

    if (client.publish(topic_datos, buffer, n)) {
        Serial.println("📊 Datos enviados");
    } else {
        Serial.println("✗ Error enviando datos");
    }
}

// ✅ FUNCIÓN QUE SE LLAMA DESDE EL LOOP (NO DESDE EL TICKER)
void enviarPulsoMQTT() {
    if (!client.connected()) {
        return;
    }

    JsonDocument doc;
    doc["device_id"] = device_id;
    doc["timestamp"] = millis();

    char buffer[128];
    size_t n = serializeJson(doc, buffer);

    if (client.publish(topic_heartbeat, buffer, n)) {
        Serial.println("💓 Heartbeat enviado");
    } else {
        Serial.println("✗ Error heartbeat");
    }
}

// ✅ FUNCIÓN QUE LLAMA EL TICKER (SOLO ACTIVA FLAG)
void marcarHeartbeat() {
    debeEnviarHeartbeat = true;
}

// ============================================================================
// FUNCIONES DE PANTALLA
// ============================================================================

void graficar() {
    // Si cambia el modo de display, limpiar y resetear todo
    if (GRUADISPLAY != prevGRUADISPLAY) {
        lcd.clear();
        prevGRUADISPLAY = GRUADISPLAY;
        prevCOIN = -1; prevCONTSALIDA = -1; prevBANK_lcd = -1; prevPAGO = -1;
        prevBANKTIEMPO = -1; prevCREDITO = -1;
    }

    if (GRUADISPLAY == 0) {
        // Escribir etiquetas estáticas solo la primera vez (ANTES de los valores)
        if (prevCOIN == -1) {
            lcd.setCursor(0, 0); lcd.print("PJ:");
            lcd.setCursor(9, 0); lcd.print("PP:");
            lcd.setCursor(0, 1); lcd.print("PA:");
            lcd.setCursor(9, 1); lcd.print("BK:");
            // Forzar actualización de todos los valores
            prevCOIN = -1; prevCONTSALIDA = -1; prevBANK_lcd = -1; 
            prevPAGO = -1; prevBANKTIEMPO = -1;
        }
        
        // Actualizar valores solo si cambiaron
        if (COIN != prevCOIN || prevCOIN == -1) { 
            lcd.setCursor(3, 0); lcd.print("      "); 
            lcd.setCursor(3, 0); lcd.print(COIN); 
            prevCOIN = COIN; 
        }
        if (CONTSALIDA != prevCONTSALIDA || prevCONTSALIDA == -1) { 
            lcd.setCursor(12, 0); lcd.print("    "); 
            lcd.setCursor(12, 0); lcd.print(CONTSALIDA); 
            prevCONTSALIDA = CONTSALIDA; 
        }
        if (PAGO != prevPAGO || prevPAGO == -1) { 
            lcd.setCursor(3, 1); lcd.print("   "); 
            lcd.setCursor(3, 1); lcd.print(PAGO); 
            prevPAGO = PAGO; 
        }
        if (BANKTIEMPO != prevBANKTIEMPO || prevBANKTIEMPO == -1) { 
            lcd.setCursor(7, 1); lcd.print("  "); 
            lcd.setCursor(7, 1); lcd.print(BANKTIEMPO); 
            prevBANKTIEMPO = BANKTIEMPO; 
        }
        if (BANK != prevBANK_lcd || prevBANK_lcd == -1) { 
            lcd.setCursor(12, 1); lcd.print("     "); 
            lcd.setCursor(12, 1); lcd.print(BANK); 
            prevBANK_lcd = BANK; 
        }
    }

    if (GRUADISPLAY == 1) {
        // Escribir etiqueta estática solo la primera vez (ANTES del valor)
        if (prevCREDITO == -1) {
            lcd.setCursor(0, 0); lcd.print("Credito");
        }
        
        // Actualizar valor
        if (CREDITO != prevCREDITO || prevCREDITO == -1) {
            lcd.setCursor(8, 0); lcd.print("        ");
            lcd.setCursor(8, 0); lcd.print(CREDITO);
            prevCREDITO = CREDITO;
        }
    }
}

// ============================================================================
// FUNCIONES DE JUEGO
// ============================================================================

void leecoin() {
    while (digitalRead(PIN_COIN) == LOW && AUXCOIN < 5) {
        AUXCOIN++;
        delay(1);
        if (digitalRead(PIN_COIN) == HIGH) AUXCOIN = 0;
    }
    if (AUXCOIN == 5 && AUX2COIN == LOW) {
        CREDITO++;
        AUX2COIN = HIGH;
        graficar();
    }
    while (digitalRead(PIN_COIN) == HIGH && AUXCOIN > 0) {
        AUXCOIN--;
        delay(2);
        if (digitalRead(PIN_COIN) == LOW) AUXCOIN = 5;
    }
    if (AUXCOIN == 0 && AUX2COIN == HIGH) AUX2COIN = LOW;
}

void leerbarrera() {
    if (BARRERAAUX2 == 1) {
        digitalWrite(PIN_TRIGER, LOW);
        delayMicroseconds(2);
        digitalWrite(PIN_TRIGER, HIGH);
        delayMicroseconds(10);
        tiempo = pulseIn(PIN_ECHO, HIGH);
        distancia = tiempo / 10;

        if ((distancia + 130) < RDISTANCIA || (distancia - 130) > RDISTANCIA) {
            BARRERA = HIGH;
        }

        if (BARRERA == HIGH) {
            lcd.setCursor(0, 1);
            lcd.print("###");
            delay(2500);

            if (digitalRead(PIN_DATO12) == HIGH) {
                CONTSALIDA++;
                PPFIJO++;
                BANK -= PAGO;
                BARRERA = LOW;
                BARRERAAUX = HIGH;
                graficar();
                putEEPROM32(5, CONTSALIDA);
                putEEPROM32(29, PPFIJO);
                putEEPROM32_signed(9, BANK);
            }
        }
    }

    if (BARRERAAUX2 == 0) {
        if (digitalRead(PIN_DATO7) == HIGH) BARRERA = HIGH;

        if (BARRERA == HIGH) {
            AUXSIM = HIGH;
            lcd.setCursor(0, 1);
            lcd.print("###");
            delay(1000);
            CONTSALIDA++;
            PPFIJO++;
            BANK -= PAGO;
            BARRERA = LOW;
            BARRERAAUX = HIGH;
            graficar();
            putEEPROM32(5, CONTSALIDA);
            putEEPROM32(29, PPFIJO);
            putEEPROM32_signed(9, BANK);
        }
    }
}

void ajustebarrera() {
    if (BARRERAAUX2 == 1) {
        digitalWrite(PIN_TRIGER, LOW);
        delayMicroseconds(2);
        digitalWrite(PIN_TRIGER, HIGH);
        delayMicroseconds(10);
        tiempo = pulseIn(PIN_ECHO, HIGH);
        distancia = tiempo / 10;

        if ((distancia + 130) < RDISTANCIA || (distancia - 130) > RDISTANCIA) {
            BARRERA = HIGH;
        }

        if (BARRERA == HIGH) {
            lcd.setCursor(0, 1);
            lcd.print("###");
            delay(1000);
        }
    }

    if (BARRERAAUX2 == 0) {
        if (digitalRead(PIN_DATO7) == HIGH) BARRERA = HIGH;

        if (BARRERA == HIGH) {
            AUXSIM = HIGH;
            lcd.setCursor(0, 1);
            lcd.print("###");
            delay(1000);
        }
    }
}

bool leerBotonConDebounce(int pin, int delayMs = 50) {
    if (digitalRead(pin) == LOW) {
        delay(delayMs);
        if (digitalRead(pin) == LOW) {
            while (digitalRead(pin) == LOW) {
                delay(10);
            }
            delay(50);
            return true;
        }
    }
    return false;
}

void programar() {
    Serial.println("menu de programacion");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VERSION 1.7");
    lcd.setCursor(0, 1);
    lcd.print("24/5/24");
    delay(1000);

    while (digitalRead(PIN_DATO3) == LOW) { delay(20); }

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("PJ:");
    lcd.setCursor(4, 0); lcd.print(PJFIJO);
    lcd.setCursor(0, 1); lcd.print("PP:");
    lcd.setCursor(4, 1); lcd.print(PPFIJO);
    delay(500);
    while (digitalRead(PIN_DATO3) == HIGH) { delay(20); }
    while (digitalRead(PIN_DATO3) == LOW) { delay(20); }

    // BORRA CONTADORES
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("BORRA CONTADORES");
    lcd.setCursor(0, 1); lcd.print("NO");
    BORRARCONTADORES = LOW;
    delay(500);

    while (digitalRead(PIN_DATO3) == HIGH) {
        if (leerBotonConDebounce(PIN_DATO6)) {
            BORRARCONTADORES = HIGH;
            lcd.setCursor(0, 1); lcd.print("SI ");
        }
        if (leerBotonConDebounce(PIN_DATO10)) {
            BORRARCONTADORES = LOW;
            lcd.setCursor(0, 1); lcd.print("NO ");
        }
        delay(10);
    }
    while (digitalRead(PIN_DATO3) == LOW) { delay(20); }

    if (BORRARCONTADORES == HIGH) {
        putEEPROM32(1, 0);
        putEEPROM32(5, 0);
        putEEPROM32_signed(9, 0);
        COIN = 0;
        CONTSALIDA = 0;
        BANK = 0;
        BORRARCONTADORES = LOW;
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("BORRADOS");
        delay(1000);
    }

    // Display Modo
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Display Modo");
    if (GRUADISPLAY == 0) {
        lcd.setCursor(0, 1); lcd.print("Modo Contadores");
    }
    if (GRUADISPLAY == 1) {
        lcd.setCursor(0, 1); lcd.print("Modo Coin      ");
    }
    delay(500);

    while (digitalRead(PIN_DATO3) == HIGH) {
        if (leerBotonConDebounce(PIN_DATO6)) {
            GRUADISPLAY = 0;
            lcd.setCursor(0, 1); lcd.print("Modo Contadores");
        }
        if (leerBotonConDebounce(PIN_DATO10)) {
            GRUADISPLAY = 1;
            lcd.setCursor(0, 1); lcd.print("Modo Coin      ");
        }
        delay(10);
    }
    while (digitalRead(PIN_DATO3) == LOW) { delay(20); }
    putEEPROM(37, GRUADISPLAY);

    // AJUSTAR PAGO
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("AJUSTAR PAGO");
    lcd.setCursor(0, 1); lcd.print(PAGO);
    delay(500);

    while (digitalRead(PIN_DATO3) == HIGH) {
        lcd.setCursor(0, 1); lcd.print("    ");
        lcd.setCursor(0, 1); lcd.print(PAGO);

        if (leerBotonConDebounce(PIN_DATO6)) PAGO++;
        if (leerBotonConDebounce(PIN_DATO10)) PAGO--;
        delay(10);
    }
    while (digitalRead(PIN_DATO3) == LOW) { delay(20); }
    putEEPROM(13, PAGO);

    // AJUSTAR TIEMPO
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("AJUSTAR TIEMPO");
    lcd.setCursor(0, 1); lcd.print(TIEMPO);
    delay(500);

    while (digitalRead(PIN_DATO3) == HIGH) {
        lcd.setCursor(0, 1); lcd.print("     ");
        lcd.setCursor(0, 1); lcd.print(TIEMPO);

        if (leerBotonConDebounce(PIN_DATO6) && TIEMPO < 5000) TIEMPO += 10;
        if (leerBotonConDebounce(PIN_DATO10) && TIEMPO > 500) TIEMPO -= 10;
        delay(10);
    }
    while (digitalRead(PIN_DATO3) == LOW) { delay(20); }
    putEEPROM(17, TIEMPO);

    // TIEMPO F. FUERTE
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("TIEMPO F. FUERTE");
    lcd.setCursor(0, 1); lcd.print(TIEMPO5);
    delay(500);

    while (digitalRead(PIN_DATO3) == HIGH) {
        lcd.setCursor(0, 1); lcd.print("     ");
        lcd.setCursor(0, 1); lcd.print(TIEMPO5);

        if (leerBotonConDebounce(PIN_DATO6) && TIEMPO5 < 5000) TIEMPO5 += 10;
        if (leerBotonConDebounce(PIN_DATO10) && TIEMPO5 > 0) TIEMPO5 -= 10;
        delay(10);
    }
    while (digitalRead(PIN_DATO3) == LOW) { delay(20); }
    putEEPROM(41, TIEMPO5);

    // AJUSTAR FUERZA
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("AJUSTAR FUERZA");
    lcd.setCursor(0, 1); lcd.print(FUERZA);
    delay(500);

    while (digitalRead(PIN_DATO3) == HIGH) {
        lcd.setCursor(0, 1); lcd.print("   ");
        lcd.setCursor(0, 1); lcd.print(FUERZA);

        if (leerBotonConDebounce(PIN_DATO6) && FUERZA < 101) FUERZA++;
        if (leerBotonConDebounce(PIN_DATO10) && FUERZA > 5) FUERZA--;
        delay(10);
    }
    while (digitalRead(PIN_DATO3) == LOW) { delay(20); }
    putEEPROM(21, FUERZA);

    // TIPO BARRERA
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("TIPO BARRERA");
    if (BARRERAAUX2 == 0) {
        lcd.setCursor(0, 1); lcd.print("INFRARROJO   ");
    }
    if (BARRERAAUX2 == 1) {
        lcd.setCursor(0, 1); lcd.print("ULTRASONIDO  ");
    }
    delay(500);

    while (digitalRead(PIN_DATO3) == HIGH) {
        if (leerBotonConDebounce(PIN_DATO6)) {
            BARRERAAUX2 = 0;
            lcd.setCursor(0, 1); lcd.print("INFRARROJO   ");
        }
        if (leerBotonConDebounce(PIN_DATO10)) {
            BARRERAAUX2 = 1;
            lcd.setCursor(0, 1); lcd.print("ULTRASONIDO  ");
        }
        delay(10);
    }
    while (digitalRead(PIN_DATO3) == LOW) { delay(20); }
    putEEPROM(33, BARRERAAUX2);

    // PRUEBA BARRERA
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("PRUEBA BARRERA");
    delay(500);

    while (digitalRead(PIN_DATO3) == HIGH) {
        lcd.setCursor(0, 1); lcd.print("        ");
        lcd.setCursor(0, 1); lcd.print(distancia);
        lcd.setCursor(8, 1); lcd.print(RDISTANCIA);
        ajustebarrera();

        if (BARRERA == HIGH) {
            lcd.setCursor(0, 1); lcd.print("###");
            delay(1000);
            BARRERA = LOW;
        }
        delay(50);
    }

    while (digitalRead(PIN_DATO3) == LOW) { delay(20); }
    
    prevCOIN = -1;
    prevCONTSALIDA = -1;
    prevBANK_lcd = -1;
    prevPAGO = -1;
    prevBANKTIEMPO = -1;
    prevCREDITO = -1;
    prevGRUADISPLAY = -1;
    
    graficar();
    delay(500);
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);

    analogWriteFrequency(PIN_PINZA_PWM, PWM_FREQUENCY);

    pinMode(PIN_TRIGER, OUTPUT);
    pinMode(PIN_ECHO, INPUT);
    pinMode(PIN_DATO7, INPUT_PULLUP);
    pinMode(PIN_DATO5, OUTPUT);
    pinMode(PIN_DATO3, INPUT_PULLUP);
    pinMode(PIN_PINZA_PWM, OUTPUT);
    pinMode(PIN_PINZA_ENABLE, INPUT_PULLUP);
    pinMode(PIN_DATO11, OUTPUT);
    pinMode(PIN_DATO6, INPUT_PULLUP);
    pinMode(PIN_DATO10, INPUT_PULLUP);
    pinMode(PIN_DATO12, INPUT_PULLUP);
    pinMode(PIN_COIN, INPUT_PULLUP);

    lcd.init();
    lcd.backlight();

    EEPROM.begin(EEPROM_SIZE);

    EEPROM.get(45, INICIO);
    if (INICIO != EEPROM_INIT_CHECK_VALUE) {
        putEEPROM32(1, 0);
        putEEPROM32(5, 0);
        putEEPROM32_signed(9, 0);
        putEEPROM(13, 12);
        putEEPROM(17, 2000);
        putEEPROM(21, 50);
        putEEPROM32(25, 0);
        putEEPROM32(29, 0);
        putEEPROM(33, 0);
        putEEPROM(37, 0);
        putEEPROM(41, 0);
        putEEPROM(45, EEPROM_INIT_CHECK_VALUE);
    }

    EEPROM.get(1, COIN);
    EEPROM.get(5, CONTSALIDA);
    EEPROM.get(9, BANK);
    EEPROM.get(13, PAGO);
    EEPROM.get(17, TIEMPO);
    EEPROM.get(21, FUERZA);
    EEPROM.get(25, PJFIJO);
    EEPROM.get(29, PPFIJO);
    EEPROM.get(33, BARRERAAUX2);
    EEPROM.get(37, GRUADISPLAY);
    EEPROM.get(41, TIEMPO5);

    if (digitalRead(PIN_DATO3) == LOW) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("CONT.FIJOS");
        lcd.setCursor(0, 1); lcd.print("PJ:");
        lcd.setCursor(3, 1); lcd.print(PJFIJO);
        lcd.setCursor(9, 1); lcd.print("PP:");
        lcd.setCursor(12, 1); lcd.print(PPFIJO);
        delay(1000);
        while (digitalRead(PIN_DATO3) == HIGH) { delay(20); }
    }

    Wire.begin();
    while (B <= 10) {
        digitalWrite(PIN_TRIGER, LOW);
        delayMicroseconds(2);
        digitalWrite(PIN_TRIGER, HIGH);
        delayMicroseconds(10);
        tiempo = pulseIn(PIN_ECHO, HIGH);
        distancia = tiempo / 10;
        RDISTANCIA = distancia;
        B++;
        delay(50);
    }

    delay(100);
    graficar();

    digitalWrite(PIN_DATO11, 1);

    connectToWiFi();
    client.setServer(mqtt_server, mqtt_port);
    reconnectMQTT();

    // ✅ INICIAR TICKER CON LA FUNCIÓN CORRECTA
    tickerPulso.attach(60, marcarHeartbeat);

    prevPJFIJO = PJFIJO;
    prevPPFIJO = PPFIJO;
    prevBANK = BANK;
    
    Serial.println("✓ Setup completo");
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================

void loop() {
    int auxtbarrera = 0;

    // --- GESTIÓN DE CONEXIONES (NO BLOQUEANTE) ---
    client.loop();

    if (debeEnviarHeartbeat) {
       debeEnviarHeartbeat = false;
       enviarPulsoMQTT();
    }
    // Verificar periódicamente WiFi y MQTT
    if (millis() - lastWifiCheck > wifiCheckInterval) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi desconectado. Reconectando...");
            connectToWiFi();
        }
        if (WiFi.status() == WL_CONNECTED && !client.connected()) {
            reconnectMQTT();
        }
        lastWifiCheck = millis();
    }

    // *** VERIFICAR BOTÓN DE PROGRAMACIÓN PRIMERO ***
    if (digitalRead(PIN_DATO3) == LOW) {
        AUXDATO3++;
        delay(10);
        if (AUXDATO3 >= 30) {
            tickerPulso.detach(); // Detener heartbeat durante programación
            programar();
            tickerPulso.attach(60, enviarPulsoMQTT); // Reactivar heartbeat
            AUXDATO3 = 0;
            return; // Reiniciar loop
        }
    } else {
        AUXDATO3 = 0;
    }

    // Bucle de espera mientras la pinza no está activa
    while (digitalRead(PIN_PINZA_ENABLE) == LOW && AUX < 5) {
        client.loop();  // Mantener MQTT activo
       
        if (debeEnviarHeartbeat) {
            debeEnviarHeartbeat = false;
            enviarPulsoMQTT();
        }
        CTIEMPO++;
        if (digitalRead(PIN_PINZA_ENABLE) == HIGH) {
            AUX++;
        }
        if (digitalRead(PIN_PINZA_ENABLE) == LOW) {
            AUX = 0;
        }

        if (TIEMPO7 < 100000) {
            TIEMPO7++;
        }

        // Leer monedas
        leecoin();

        // Actualizar pantalla
        graficar();

        // Control del tiempo de juego
        if (CTIEMPO >= 18000 && BANKTIEMPO > 0) {
            CTIEMPO = 0;
            BANKTIEMPO--;
        }

        // Leer la barrera periódicamente
        if (TIEMPO8 < 200) {
            TIEMPO8++;
            if (TIEMPO8 == 199) {
                TIEMPO8 = 0;
                if (BARRERAAUX == LOW) {
                    leerbarrera();
                }
            }
        }

        // *** VERIFICAR BOTÓN TAMBIÉN DENTRO DEL WHILE ***
        if (digitalRead(PIN_DATO3) == LOW) {
            AUXDATO3++;
            if (AUXDATO3 >= 30) {
                tickerPulso.detach();
                programar();
                tickerPulso.attach(60, enviarPulsoMQTT);
                AUXDATO3 = 0;
                return;
            }
        } else {
            AUXDATO3 = 0;
        }

        delay(10); // Pequeño delay para estabilidad
    }

    // Reset de estados
    BARRERAAUX = LOW;
    AUX = 0;
    BARRERA = LOW;

    // Descontar créditos si existen
    if (CREDITO >= 1) {
        CREDITO--;
    }

    graficar();

    // Incrementar contadores
    COIN++;
    BANK++;
    PJFIJO++;

    if (BANKTIEMPO < 10) {
        BANKTIEMPO++;
    }

    // Guardar contadores en EEPROM
    putEEPROM32_signed(9, BANK);
    putEEPROM32(25, PJFIJO);
    putEEPROM32(1, COIN);

    // Enviar datos por MQTT si hay cambios
    if (PJFIJO != prevPJFIJO || PPFIJO != prevPPFIJO || BANK != prevBANK) {
        enviarDatosMQTT(PAGO, PJFIJO, PPFIJO, BANK);
        prevPJFIJO = PJFIJO;
        prevPPFIJO = PPFIJO;
        prevBANK = BANK;
    }

    Z = random(5);

    // LÓGICA DE LA PINZA - CON PAGO
    if (PAGO <= BANK && Z <= 3) {
        analogWrite(PIN_PINZA_PWM, 250);
        delay(2000);
        auxtbarrera = HIGH;

        while (auxtbarrera == HIGH) {
            while (X < 3000) {
                if (digitalRead(PIN_PINZA_ENABLE) == HIGH) {
                    X = 0;
                }
                if (X == 150) {
                    analogWrite(PIN_PINZA_PWM, 0);
                }
                if (digitalRead(PIN_PINZA_ENABLE) == LOW) {
                    X++;
                    delay(1);
                }

                if (TIEMPO8 <= 20) {
                    TIEMPO8++;
                }
                if (TIEMPO8 >= 19) {
                    TIEMPO8 = 0;
                    if (BARRERAAUX == LOW) {
                        leerbarrera();
                    }
                }

                if (X == 2998) {
                    auxtbarrera = LOW;
                }

                if (BARRERAAUX == HIGH) {
                    auxtbarrera = LOW;
                    break;
                }
            }
        }
    }
    // LÓGICA DE LA PINZA - SIN PAGO
    else {
        analogWrite(PIN_PINZA_PWM, 255);
        delay(TIEMPO5);

        FUERZAAUX2 = FUERZA;
        if (BANK <= -10) {
            FUERZAAUX2 = FUERZA * 0.8;
        }

        FUERZAV = random(FUERZAAUX2 * 1.8, FUERZAAUX2 * 2.5);
        FUERZAAUX = (FUERZAV - FUERZAAUX2) / 10;

        TIEMPOAUX = 0;
        TIEMPOAUX1 = (TIEMPO / 10);

        while (TIEMPOAUX <= 10) {
            FUERZAV -= FUERZAAUX;
            analogWrite(PIN_PINZA_PWM, FUERZAV);
            TIEMPOAUX++;
            delay(TIEMPOAUX1);
        }

        analogWrite(PIN_PINZA_PWM, FUERZA * 1.3);
        delay(300);
        analogWrite(PIN_PINZA_PWM, FUERZA);
        delay(100);
        analogWrite(PIN_PINZA_PWM, FUERZA * 1.3);

        auxtbarrera = HIGH;

        while (auxtbarrera == HIGH) {
            while (X < 3000) {
                if (digitalRead(PIN_PINZA_ENABLE) == HIGH) {
                    X = 0;
                }
                if (X == 150) {
                    analogWrite(PIN_PINZA_PWM, 0);
                }
                if (digitalRead(PIN_PINZA_ENABLE) == LOW) {
                    X++;
                    delay(1);
                }

                if (TIEMPO8 <= 20) {
                    TIEMPO8++;
                }
                if (TIEMPO8 >= 19) {
                    TIEMPO8 = 0;
                    if (BARRERAAUX == LOW) {
                        leerbarrera();
                    }
                }

                if (X == 2998) {
                    auxtbarrera = LOW;
                }

                if (BARRERAAUX == HIGH) {
                    auxtbarrera = LOW;
                    break;
                }
            }
        }
    }

    // Apagar la pinza
    analogWrite(PIN_PINZA_PWM, 0);
    TIEMPO8 = 0;
    X = 0;
}
