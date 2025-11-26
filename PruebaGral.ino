// Gold Digger - Versión Híbrida (Dual Memory)
// Noviembre 2025
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
const char* device_id = "ESP32_005";
const char* ssid = "ezee";
const char* password = "39090169";

// DATOS DE TU BROKER
const char* mqtt_server = "broker.emqx.io"; 
const int mqtt_port = 1883;

const char* topic_datos = "maquinas/ESP32_005/datos";
const char* topic_heartbeat = "maquinas/ESP32_005/heartbeat";

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
#define DATO12 27
#define ECOIN 26

#define EEPROM_INIT_CHECK_VALUE 35
#define PWM_FREQUENCY 100

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
int CREDITO = 0;
int AUXCOIN = 0;
int AUX2COIN = 0;
int16_t FUERZA = 50;
int16_t INICIO = 0;
int BORRARCONTADORES = LOW;
unsigned int PJFIJO = 0;
unsigned int PPFIJO = 0;
int BARRERAAUX = 0;
float FUERZAAUX = 0;
float FUERZAV = 0;
int TIEMPOAUX1 = 0;
int FUERZAAUX2 = 0;
int16_t GRUADISPLAY = 0;
int16_t BARRERAAUX2 = 0;
char DAT;
int TIEMPO7 = 0;
unsigned long TIEMPO8 = 0;
int TIEMPO9 = 0;
int AUXSIM = 0;
int16_t TIEMPO5 = 0;
int prevPJFIJO = 0;
int prevPPFIJO = 0;
int prevBANK = 0;
int AUXDATO3 = 0;

volatile bool debeEnviarHeartbeat = false;
unsigned long lastWifiCheck = 0;
const long wifiCheckInterval = 30000;

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
      EEPROM.commit();
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
      EEPROM.commit();
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

void connectToWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.disconnect(true);
    delay(1000);
    Serial.println("Conectando a WiFi...");
    WiFi.begin(ssid, password);
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 10) {
        delay(500); Serial.print("."); retry++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Conectado");
        Serial.print("IP: "); Serial.println(WiFi.localIP());
    }
}

void reconnectMQTT() {
    if (!client.connected()) {
        Serial.print("Conectando MQTT...");
        if (client.connect(device_id, "maquinas/status", 1, true, "offline")) {
            Serial.println("¡Conectado!");
            client.publish("maquinas/status", "online");
        } else {
            Serial.print("Fallo rc="); Serial.println(client.state());
        }
    }
}

void enviarDatosMQTT(unsigned int dato1, unsigned int dato2, unsigned int dato3, int dato4) {
    if (!client.connected()) return;
    JsonDocument doc;
    doc["device_id"] = device_id;
    doc["pago"] = dato1;
    doc["partidas_jugadas"] = dato2;
    doc["premios_pagados"] = dato3;
    doc["banco"] = dato4;
    char buffer[256];
    serializeJson(doc, buffer);
    if(client.publish(topic_datos, buffer)) Serial.println("Datos MQTT enviados");
}

void activarHeartbeat() { debeEnviarHeartbeat = true; }

void enviarPulso() {
    if (!client.connected()) return;
    JsonDocument doc;
    doc["device_id"] = device_id;
    doc["status"] = "online";
    char buffer[128];
    serializeJson(doc, buffer);
    client.publish(topic_heartbeat, buffer);
    Serial.println("Heartbeat enviado");
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
        lcd.setCursor(0, 0); lcd.print("Credito");
        lcd.setCursor(8, 0); lcd.print(CREDITO);
    }
}

void leecoin() {
    while (digitalRead(ECOIN) == LOW && AUXCOIN < 5) {
        AUXCOIN++; delay(1);
        if (digitalRead(ECOIN) == HIGH) AUXCOIN = 0;
    }
    if (AUXCOIN == 5 && AUX2COIN == LOW) {
        CREDITO++; AUX2COIN = HIGH; graficar();
    }
    while (digitalRead(ECOIN) == HIGH && AUXCOIN > 0) {
        AUXCOIN--; delay(2);
        if (digitalRead(ECOIN) == LOW) AUXCOIN = 5;
    }
    if (AUXCOIN == 0 && AUX2COIN == HIGH) AUX2COIN = LOW;
}

void leerbarrera() {
    if (BARRERAAUX2 == 1) {
        digitalWrite(triger, LOW); delayMicroseconds(2);
        digitalWrite(triger, HIGH); delayMicroseconds(10);
        tiempo = pulseIn(echo, HIGH);
        distancia = tiempo / 10;
        if ((distancia + 130) < RDISTANCIA || (distancia - 130) > RDISTANCIA) BARRERA = HIGH;
        if (BARRERA == HIGH) {
            lcd.setCursor(0, 1); lcd.print("###"); delay(2500); graficar();
            if (digitalRead(DATO12) == HIGH) {
                CONTSALIDA++; PPFIJO++; BANK -= PAGO;
                BARRERA = LOW; BARRERAAUX = HIGH;
                graficar();
                putEEPROMUint(5, CONTSALIDA);
                putEEPROMUint(29, PPFIJO);
                putEEPROM(9, BANK);
            }
        }
    }
    if (BARRERAAUX2 == 0) {
        if (digitalRead(DATO7) == HIGH) BARRERA = HIGH;
        if (BARRERA == HIGH) {
            AUXSIM = HIGH; lcd.setCursor(0, 1); lcd.print("###"); delay(1000);
            CONTSALIDA++; PPFIJO++; BANK -= PAGO;
            BARRERA = LOW; BARRERAAUX = HIGH;
            graficar();
            putEEPROMUint(5, CONTSALIDA);
            putEEPROMUint(29, PPFIJO);
            putEEPROM(9, BANK);
        }
    }
}

void ajustebarrera() {
    if (BARRERAAUX2 == 1) {
        digitalWrite(triger, LOW); delayMicroseconds(2);
        digitalWrite(triger, HIGH); delayMicroseconds(10);
        tiempo = pulseIn(echo, HIGH);
        distancia = tiempo / 10;
        if ((distancia + 130) < RDISTANCIA || (distancia - 130) > RDISTANCIA) BARRERA = HIGH;
        if (BARRERA == HIGH) { lcd.setCursor(0, 1); lcd.print("###"); delay(1000); }
    }
    if (BARRERAAUX2 == 0) {
        if (digitalRead(DATO7) == HIGH) BARRERA = HIGH;
        if (BARRERA == HIGH) { AUXSIM = HIGH; lcd.setCursor(0, 1); lcd.print("###"); delay(1000); }
    }
}

bool leerBotonConDebounce(int pin, int delayMs = 50) {
    if (digitalRead(pin) == LOW) {
        delay(delayMs);
        if (digitalRead(pin) == LOW) {
            while (digitalRead(pin) == LOW) delay(10);
            delay(50); return true;
        }
    }
    return false;
}

   void programar() {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("VERSION 1.7");
    lcd.setCursor(0,1);
    lcd.print("24/5/24");
    delay(500);
    
    // Esperar a que se suelte DATO3 si está presionado
    while (digitalRead(DATO3) == LOW) {
        delay(20);
    }
    delay(100);  // Delay adicional
    
    // Esperar a que se presione DATO3
    while (digitalRead(DATO3) == HIGH) {
        delay(20);
    }
    // Esperar a que se suelte
    while (digitalRead(DATO3) == LOW) {
        delay(20);
    }
    delay(100);

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
    delay(500);
    
    while (digitalRead(DATO3) == HIGH) {
        delay(20);
    }
    while (digitalRead(DATO3) == LOW) {
        delay(20);
    }
    delay(100);
    
    GRUADISPLAY = 0;
    graficar();
    delay(500);
    
    while (digitalRead(DATO3) == HIGH) {
        delay(20);
    }
    while (digitalRead(DATO3) == LOW) {
        delay(20);
    }
    delay(100);
    
    GRUADISPLAY = getEEPROM(37);

    // BORRA CONTADORES con debounce mejorado
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("BORRA CONTADORES");
    lcd.setCursor(0,1);
    lcd.print("NO");
    delay(500);
    
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
    }
    
    // Esperar a que se suelte DATO3
    while (digitalRead(DATO3) == LOW) {
        delay(20);
    }
    delay(100);
    
    if (BORRARCONTADORES == HIGH) {
        putEEPROMUint(1, 0);
        putEEPROMUint(5, 0);
        putEEPROM(9, 0);
        COIN = 0;
        CONTSALIDA = 0;
        BANK = 0;
        BORRARCONTADORES = LOW;
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("BORRADOS");
        delay(1000);
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
    delay(500);
    
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
    }
    
    while (digitalRead(DATO3) == LOW) {
        delay(20);
    }
    delay(100);
    
    putEEPROM(37, GRUADISPLAY);

    // AJUSTAR PAGO
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("AJUSTAR PAGO");
    delay(500);
    
    while (digitalRead(DATO3) == HIGH) {
        lcd.setCursor(0,1);
        lcd.print("   ");  // Limpiar
        lcd.setCursor(0,1);
        lcd.print(PAGO);
        
        if (leerBotonConDebounce(DATO6)) {
            PAGO++;
        }
        if (leerBotonConDebounce(DATO10)) {
            PAGO--;
        }
        delay(10);
    }
    
    while (digitalRead(DATO3) == LOW) {
        delay(20);
    }
    delay(100);
    
    putEEPROM(13, PAGO);

    // AJUSTAR TIEMPO
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("AJUSTAR TIEMPO");
    delay(500);
    
    while (digitalRead(DATO3) == HIGH) {
        lcd.setCursor(0,1);
        lcd.print("     ");  // Limpiar
        lcd.setCursor(0,1);
        lcd.print(TIEMPO);
        
        if (leerBotonConDebounce(DATO6) && TIEMPO < 5000) {
            TIEMPO += 10;
        }
        if (leerBotonConDebounce(DATO10) && TIEMPO > 500) {
            TIEMPO -= 10;
        }
        delay(10);
    }
    
    while (digitalRead(DATO3) == LOW) {
        delay(20);
    }
    delay(100);
    
    putEEPROM(17, TIEMPO);

    // TIEMPO F. FUERTE
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("TIEMPO F. FUERTE");
    delay(500);
    
    while (digitalRead(DATO3) == HIGH) {
        lcd.setCursor(0,1);
        lcd.print("     ");  // Limpiar
        lcd.setCursor(0,1);
        lcd.print(TIEMPO5);
        
        if (leerBotonConDebounce(DATO6) && TIEMPO5 < 5000) {
            TIEMPO5 += 10;
        }
        if (leerBotonConDebounce(DATO10) && TIEMPO5 > 0) {
            TIEMPO5 -= 10;
        }
        delay(10);
    }
    
    while (digitalRead(DATO3) == LOW) {
        delay(20);
    }
    delay(100);
    
    putEEPROM(41, TIEMPO5);

    // AJUSTAR FUERZA
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("AJUSTAR FUERZA");
    delay(500);
    
    while (digitalRead(DATO3) == HIGH) {
        lcd.setCursor(0,1);
        lcd.print("   ");  // Limpiar
        lcd.setCursor(0,1);
        lcd.print(FUERZA);
        
        if (leerBotonConDebounce(DATO6) && FUERZA < 101) {
            FUERZA++;
        }
        if (leerBotonConDebounce(DATO10) && FUERZA > 5) {
            FUERZA--;
        }
        delay(10);
    }
    
    while (digitalRead(DATO3) == LOW) {
        delay(20);
    }
    delay(100);
    
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
    delay(500);
    
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
    }
    
    while (digitalRead(DATO3) == LOW) {
        delay(20);
    }
    delay(100);
    
    putEEPROM(33, BARRERAAUX2);

    // PRUEBA BARRERA
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("PRUEBA BARRERA");
    delay(500);
    
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
            delay(1000);
            BARRERA = LOW;
        }
        delay(50);
    }
    
    while (digitalRead(DATO3) == LOW) {
        delay(20);
    }
    delay(100);

    graficar();
    delay(500);
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

    analogWriteFrequency(EPINZA, 100);
    lcd.init(); lcd.backlight();

    pinMode(triger, OUTPUT); pinMode(echo, INPUT);
    pinMode(DATO7, INPUT_PULLUP); pinMode(DATO5, OUTPUT);
    pinMode(DATO3, INPUT_PULLUP); pinMode(SPINZA, OUTPUT);
    pinMode(EPINZA, INPUT_PULLUP); pinMode(DATO11, OUTPUT);
    pinMode(DATO6, INPUT_PULLUP); pinMode(DATO10, INPUT_PULLUP);
    pinMode(DATO12, INPUT_PULLUP); pinMode(ECOIN, INPUT_PULLUP);
    
    // Leer EEPROM (Automático según modo)
    INICIO = getEEPROM(45);
    if (INICIO != 35) {
        Serial.println("Inicializando memoria por primera vez...");
        putEEPROMUint(1, 0); putEEPROMUint(5, 0); putEEPROM(9, 0);
        putEEPROM(13, 12); putEEPROM(17, 2000); putEEPROM(21, 50);
        putEEPROMUint(25, 0); putEEPROMUint(29, 0); putEEPROM(33, 0);
        putEEPROM(37, 0); putEEPROM(41, 0); putEEPROM(45, 35);
    }

    COIN = getEEPROMUint(1); CONTSALIDA = getEEPROMUint(5);
    BANK = getEEPROM(9); PAGO = getEEPROM(13);
    TIEMPO = getEEPROM(17); FUERZA = getEEPROM(21);
    PJFIJO = getEEPROMUint(25); PPFIJO = getEEPROMUint(29);
    BARRERAAUX2 = getEEPROM(33); GRUADISPLAY = getEEPROM(37);
    TIEMPO5 = getEEPROM(41);
    
    if (digitalRead(DATO3) == LOW){ 
        lcd.clear(); lcd.setCursor(0,0); lcd.print("CONT.FIJOS"); 
        lcd.setCursor(0,1); lcd.print("PJ:"); lcd.print(PJFIJO); 
        lcd.print(" PP:"); lcd.print(PPFIJO); delay(1000);
        while (digitalRead(DATO3) == HIGH) delay(20);
    }
    delay(1000);
    
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
    tickerPulso.attach(60, activarHeartbeat); 
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!client.connected()) reconnectMQTT();
        client.loop();
    }
    
    if (millis() - lastWifiCheck > wifiCheckInterval) {
       if (WiFi.status() != WL_CONNECTED) connectToWiFi();
       lastWifiCheck = millis();
    }
    
    if (debeEnviarHeartbeat) {
        enviarPulso();
        debeEnviarHeartbeat = false;
    }

    unsigned int dato1, dato2, dato3;
    int dato4, auxtbarrera = 0;

    graficar(); 

    // BUCLE DE ESPERA
    while (digitalRead(EPINZA) == LOW && AUX < 5) {
        client.loop();

        CTIEMPO++;
        if (digitalRead(EPINZA) == HIGH) AUX++;
        if (digitalRead(EPINZA) == LOW) AUX = 0;
        if (TIEMPO7 < 100000) TIEMPO7++;

        leecoin();

        // DETECCIÓN DE CAMBIOS Y ENVÍO MQTT
        if (PJFIJO != prevPJFIJO || PPFIJO != prevPPFIJO || BANK != prevBANK) {
            enviarDatosMQTT(PAGO, PJFIJO, PPFIJO, BANK);
            prevPJFIJO = PJFIJO; prevPPFIJO = PPFIJO; prevBANK = BANK;
        }

        if (digitalRead(DATO3) == LOW) AUXDATO3++;
        else AUXDATO3 = 0;

        if (digitalRead(DATO3) == LOW && AUXDATO3 == 30) {
            tickerPulso.detach();
            programar();
            tickerPulso.attach(60, activarHeartbeat);
            AUXDATO3 = 0;
        }

        if (CTIEMPO >= 18000 && BANKTIEMPO > 0) {
            CTIEMPO = 0; BANKTIEMPO--;
        }

        if (TIEMPO8 < 200) {
            TIEMPO8++;
            if (TIEMPO8 == 199) {
                TIEMPO8 = 0;
                if (BARRERAAUX == LOW) leerbarrera();
            }
        }
    }

    // --- SECUENCIA DE JUEGO ---
    BARRERAAUX = LOW; AUX = 0; BARRERA = LOW;
    if (CREDITO >= 1) CREDITO--;
    graficar();

    COIN++; BANK++; PJFIJO++;
    if (BANKTIEMPO < 10) BANKTIEMPO++;

    // GUARDADO (Automático según modo)
    putEEPROM(9, BANK);
    putEEPROMUint(25, PJFIJO);
    putEEPROMUint(1, COIN);

    Z = random(5);

    // PINZA CON PREMIO
    if (PAGO <= BANK && Z <= 3) {
        analogWrite(SPINZA, 250); delay(2000);
        auxtbarrera = HIGH;
        while (auxtbarrera == HIGH) {
            while(X < 3000){
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
            }
        }
    } 
    // PINZA SIN PREMIO
    else {
        analogWrite(SPINZA, 255); delay(TIEMPO5);
        FUERZAAUX2 = FUERZA;
        if (BANK <= -10) FUERZAAUX2 = FUERZA * 0.8;

        FUERZAV = random(FUERZAAUX2 * 1.8, FUERZAAUX2 * 2.5);
        FUERZAAUX = (FUERZAV - FUERZAAUX2) / 10;
        TIEMPOAUX = 0; TIEMPOAUX1 = (TIEMPO / 10);

        while (TIEMPOAUX <= 10) {
            FUERZAV -= FUERZAAUX;
            analogWrite(SPINZA, FUERZAV);
            TIEMPOAUX++; delay(TIEMPOAUX1);
        }

        analogWrite(SPINZA, FUERZA * 1.3); delay(300);
        analogWrite(SPINZA, FUERZA); delay(100);
        analogWrite(SPINZA, FUERZA * 1.3);
        auxtbarrera = HIGH;

        while (auxtbarrera == HIGH) {
            while(X < 3000){
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
            }
        }
    }

    analogWrite(SPINZA, 0);
    TIEMPO8 = 0; X = 0; A = 0;
}
