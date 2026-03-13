// Gigga
// Marzo 2026
// Protocolo: MQTT
// Memoria: Seleccionable (Interna ESP32 o Externa I2C)

// ==========================================
// 🎚️ CONFIGURACIÓN DE MEMORIA (TOCA ACA)
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
#include <EEPROM.h>

// ============================================================================
// EEPROM - DIRECCIONES
// ============================================================================
#define DIR_JUGADAS       1
#define DIR_PREMIOS       5
#define DIR_BANCO         9
#define DIR_PAGO          13
#define DIR_T_AGARRE      17
#define DIR_FUERZA        21
#define DIR_JUGADAS_TOT   25
#define DIR_PREMIOS_TOT   29
#define DIR_TIPO_BARRERA  33
#define DIR_MODO_DISP     37
#define DIR_T_FUERTE      41
#define DIR_INICIO_CHECK  45
#define VAL_INICIO_CHECK  35

// ============================================================================
// EEPROM - CONFIGURACIÓN
// ============================================================================
#define EEPROM_DIR_I2C  0x50
#define EEPROM_TAM      512

// ============================================================================
// PINES
// ============================================================================
#define PIN_TRIGGER      13   // Trigger ultrasonido
#define PIN_ECHO         12   // Echo ultrasonido
#define PIN_LUZ          19   // Salida de luz
#define PIN_BARR_IR      14   // Sensor barrera infrarrojo
#define PIN_MOTOR        17   // Motor pinza (PWM)
#define PIN_SENS_POS     16   // Sensor posición pinza
#define PIN_BTN_BAJA     35   // Botón bajar valor en menú
#define PIN_SENS_PREMIO  27   // Sensor detección de premio
#define PIN_BTN_SUBE     34   // Botón subir valor en menú
#define PIN_BTN_MENU      4   // Botón menú / confirmar
#define PIN_MONEDA       26   // Sensor de moneda
#define PIN_CREDITO      25   // Salida de crédito

// ============================================================================
// WIFI Y MQTT
// ============================================================================
const char* ID_DISP     = "ESP32_005";
const char* WIFI_SSID   = "FIBRA-WIFI6-229F";
const char* WIFI_CLAVE  = "46332714";
const char* MQTT_SERVER = "broker.emqx.io";
const int   MQTT_PUERTO = 1883;
const char* TOPIC_DATOS = "maquinas/ESP32_005/datos";
const char* TOPIC_PULSO = "maquinas/ESP32_005/heartbeat";

// ============================================================================
// OBJETOS
// ============================================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient   clienteWifi;
PubSubClient clienteMQTT(clienteWifi);
Ticker       tickerPulso;

// ============================================================================
// VARIABLES: CONTADORES Y BALANCE
// ============================================================================
unsigned int jugadas     = 0;   // Jugadas en sesión            (antes: COIN)
unsigned int premios     = 0;   // Premios entregados sesión    (antes: CONTSALIDA)
int16_t      banco       = 0;   // Balance crédito/débito       (antes: BANK)
unsigned int tBanco      = 0;   // Tiempo de vida del banco     (antes: BANKTIEMPO)
unsigned int jugadasTot  = 0;   // Jugadas totales acumuladas   (antes: PJFIJO)
unsigned int premiosTot  = 0;   // Premios totales acumulados   (antes: PPFIJO)

// ============================================================================
// VARIABLES: CONFIGURACIÓN DE JUEGO
// ============================================================================
int16_t pago        = 20;   // Créditos para dar premio     (antes: PAGO)
int16_t tAgarre     = 0;    // Tiempo de agarre pinza (ms)  (antes: TIEMPO)
int16_t fuerza      = 50;   // Fuerza base pinza 0-100      (antes: FUERZA)
int16_t tFuerte     = 0;    // Tiempo fuerza fuerte (ms)    (antes: TIEMPO5)
int16_t modoDisp    = 0;    // 0=Contadores 1=Crédito       (antes: GRUADISPLAY)
int16_t tipoBarr    = 0;    // 0=Infrarrojo 1=Ultrasonido   (antes: BARRERAAUX2)

// ============================================================================
// VARIABLES: SENSOR DE DISTANCIA
// ============================================================================
int distRef  = 0;   // Distancia calibrada en reposo    (antes: RDISTANCIA)
int distAct  = 0;   // Distancia medida en tiempo real  (antes: distancia)
int tEco     = 0;   // Tiempo de pulso eco en µs        (antes: tiempo)

// ============================================================================
// VARIABLES: BARRERA Y PREMIO
// ============================================================================
int barrActiva   = 0;   // Barrera detectó objeto           (antes: BARRERA)
int premioDetect = 0;   // Premio confirmado en jugada       (antes: BARRERAAUX)

// ============================================================================
// VARIABLES: MONEDA Y CRÉDITO
// ============================================================================
int creditos   = 0;   // Créditos disponibles             (antes: CREDITO)
int debMonSube = 0;   // Debounce subida moneda           (antes: AUXCOIN)
int debMonBaja = 0;   // Debounce bajada moneda           (antes: AUX2COIN)

// ============================================================================
// VARIABLES: PINZA Y FUERZA
// ============================================================================
float fuerzaVar = 0;   // Fuerza actual interpolada        (antes: FUERZAV)
float fuerzaDec = 0;   // Decremento de fuerza             (antes: FUERZAAUX)
int   fuerzaAdj = 0;   // Fuerza ajustada según banco      (antes: FUERZAAUX2)

// ============================================================================
// VARIABLES: TEMPORIZACIÓN Y BUCLES
// ============================================================================
unsigned long cntPinza   = 0;   // Contador bucle espera pinza     (antes: X)
int           debPinza   = 0;   // Debounce sensor pinza           (antes: AUX / A)
int           pasoFuerza = 0;   // Índice paso de interpolación    (antes: TIEMPOAUX)
int           delayPaso  = 0;   // Delay entre pasos de fuerza     (antes: TIEMPOAUX1)
int           cntEspera  = 0;   // Contador ciclos idle            (antes: TIEMPO7)
unsigned long tBarrCheck = 0;   // Timer chequeo barrera           (antes: TIEMPO8)
int           cntBanco   = 0;   // Contador decremento banco       (antes: CTIEMPO)
int16_t       inicio     = 0;   // Valor check inicialización      (antes: INICIO)
int           cntMenuBtn = 0;   // Contador retención botón menú   (antes: AUXDATO3)
int           simBarr    = 0;   // Flag simulación barrera IR      (antes: AUXSIM)
int           borrarCont = LOW; // Flag borrar contadores          (antes: BORRARCONTADORES)

// ============================================================================
// VARIABLES: MQTT Y WIFI
// ============================================================================
volatile bool flagPulso    = false;
unsigned long tUltReconMQTT= 0;
int prevJugadasTot         = 0;   // (antes: prevPJFIJO)
int prevPremiosTot         = 0;   // (antes: prevPPFIJO)
int prevBanco              = 0;   // (antes: prevBANK)

// ============================================================================
// FUNCIONES: EEPROM EXTERNA (I2C)
// ============================================================================
void escribirEEPROM(int dir, byte dato) {
    byte dirDev = EEPROM_DIR_I2C | ((dir >> 8) & 0x01);
    Wire.beginTransmission(dirDev);
    Wire.write((byte)(dir & 0xFF));
    Wire.endTransmission();
    Wire.requestFrom(dirDev, (byte)1);
    byte actual = 0xFF;
    if (Wire.available()) actual = Wire.read();
    if (actual != dato) {
        Wire.beginTransmission(dirDev);
        Wire.write((byte)(dir & 0xFF));
        Wire.write(dato);
        Wire.endTransmission();
        delay(5);
    }
}

byte leerEEPROM(int dir) {
    byte dato = 0xFF;
    byte dirDev = EEPROM_DIR_I2C | ((dir >> 8) & 0x01);
    Wire.beginTransmission(dirDev);
    Wire.write((byte)(dir & 0xFF));
    Wire.endTransmission();
    Wire.requestFrom(dirDev, (byte)1);
    if (Wire.available()) dato = Wire.read();
    return dato;
}

// ============================================================================
// FUNCIONES: EEPROM ALTO NIVEL (selector interno/externo)
// ============================================================================
void guardarInt(int dir, int val) {
    if (USAR_EEPROM_EXTERNA) {
        escribirEEPROM(dir,     (val >> 8) & 0xFF);
        escribirEEPROM(dir + 1,  val       & 0xFF);
    } else {
        int16_t actual;
        EEPROM.get(dir, actual);
        if (actual != val) { EEPROM.put(dir, (int16_t)val); EEPROM.commit(); }
    }
}

void guardarUInt(int dir, unsigned int val) {
    if (USAR_EEPROM_EXTERNA) {
        escribirEEPROM(dir,     (val >> 24) & 0xFF);
        escribirEEPROM(dir + 1, (val >> 16) & 0xFF);
        escribirEEPROM(dir + 2, (val >>  8) & 0xFF);
        escribirEEPROM(dir + 3,  val        & 0xFF);
    } else {
        uint32_t actual;
        EEPROM.get(dir, actual);
        if (actual != val) { EEPROM.put(dir, (uint32_t)val); EEPROM.commit(); }
    }
}

int leerInt(int dir) {
    if (USAR_EEPROM_EXTERNA) {
        return (leerEEPROM(dir) << 8) | leerEEPROM(dir + 1);
    } else {
        int16_t val; EEPROM.get(dir, val); return val;
    }
}

unsigned int leerUInt(int dir) {
    if (USAR_EEPROM_EXTERNA) {
        return ((unsigned int)leerEEPROM(dir)     << 24) |
               ((unsigned int)leerEEPROM(dir + 1) << 16) |
               ((unsigned int)leerEEPROM(dir + 2) <<  8) |
                (unsigned int)leerEEPROM(dir + 3);
    } else {
        uint32_t val; EEPROM.get(dir, val); return val;
    }
}

// ============================================================================
// FUNCIONES: WIFI Y MQTT
// ============================================================================
void conectarWifi() {
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.disconnect(true);
    delay(1000);
    Serial.println("Conectando a WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_CLAVE);
    int intentos = 0;
    while (WiFi.status() != WL_CONNECTED && intentos < 10) {
        delay(500); Serial.print("."); intentos++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Conectado");
        Serial.print("IP: "); Serial.println(WiFi.localIP());
    }
}

void reconectarMQTT() {
    if (millis() - tUltReconMQTT < 5000) return;
    tUltReconMQTT = millis();
    Serial.print("Reconectando MQTT...");
    if (clienteMQTT.connect(ID_DISP, "maquinas/status", 1, true, "offline")) {
        Serial.println("OK");
        clienteMQTT.publish("maquinas/status", "online");
    } else {
        Serial.print("Error rc="); Serial.println(clienteMQTT.state());
    }
}

void enviarDatos() {
    if (!clienteMQTT.connected()) return;
    JsonDocument doc;
    doc["device_id"] = ID_DISP;
    doc["dato1"]     = pago;
    doc["dato2"]     = jugadasTot;
    doc["dato3"]     = premiosTot;
    doc["dato4"]     = banco;
    char buf[256];
    serializeJson(doc, buf);
    if (clienteMQTT.publish(TOPIC_DATOS, buf)) Serial.println("Datos enviados MQTT");
}

void activarPulso() { flagPulso = true; }

void enviarHeartbeat() {
    if (!clienteMQTT.connected()) return;
    JsonDocument doc;
    doc["device_id"] = ID_DISP;
    doc["status"]    = "online";
    char buf[128];
    serializeJson(doc, buf);
    clienteMQTT.publish(TOPIC_PULSO, buf);
    Serial.println("Heartbeat enviado");
}

// ============================================================================
// FUNCIONES: DISPLAY
// ============================================================================
void mostrarDisplay() {
    if (modoDisp == 0) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("PJ:"); lcd.setCursor(3, 0); lcd.print(jugadas);
        lcd.setCursor(9, 0); lcd.print("PP:"); lcd.setCursor(12, 0); lcd.print(premios);
        lcd.setCursor(0, 1); lcd.print("PA:"); lcd.setCursor(3, 1); lcd.print(pago);
        lcd.setCursor(7, 1); lcd.print(tBanco);
        lcd.setCursor(9, 1); lcd.print("BK:"); lcd.setCursor(12, 1); lcd.print(banco);
    }
    if (modoDisp == 1) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("Credito");
        lcd.setCursor(8, 0); lcd.print(creditos);
    }
}

// ============================================================================
// FUNCIONES: MONEDA
// ============================================================================
void leerMoneda() {
    while (digitalRead(PIN_MONEDA) == LOW && debMonSube < 5) {
        debMonSube++; delay(1);
        if (digitalRead(PIN_MONEDA) == HIGH) debMonSube = 0;
    }
    if (debMonSube == 5 && debMonBaja == LOW) {
        creditos++; debMonBaja = HIGH; mostrarDisplay();
    }
    while (digitalRead(PIN_MONEDA) == HIGH && debMonSube > 0) {
        debMonSube--; delay(2);
        if (digitalRead(PIN_MONEDA) == LOW) debMonSube = 5;
    }
    if (debMonSube == 0 && debMonBaja == HIGH) debMonBaja = LOW;
}

// ============================================================================
// FUNCIONES: BARRERA
// ============================================================================
void medirDistancia() {
    digitalWrite(PIN_TRIGGER, LOW);  delayMicroseconds(2);
    digitalWrite(PIN_TRIGGER, HIGH); delayMicroseconds(10);
    tEco   = pulseIn(PIN_ECHO, HIGH);
    distAct = tEco / 10;
}

void leerBarrera() {
    if (tipoBarr == 1) {
        medirDistancia();
        if ((distAct + 130) < distRef || (distAct - 130) > distRef) barrActiva = HIGH;
        if (barrActiva == HIGH) {
            lcd.setCursor(0, 1); lcd.print("###"); delay(2500); mostrarDisplay();
            if (digitalRead(PIN_SENS_PREMIO) == HIGH) {
                premios++;    premiosTot++;   banco -= pago;
                barrActiva  = LOW;            premioDetect = HIGH;
                mostrarDisplay();
                guardarUInt(DIR_PREMIOS,     premios);
                guardarUInt(DIR_PREMIOS_TOT, premiosTot);
                guardarInt (DIR_BANCO,       banco);
            }
        }
    } else {
        if (digitalRead(PIN_BARR_IR) == HIGH) barrActiva = HIGH;
        if (barrActiva == HIGH) {
            simBarr = HIGH; lcd.setCursor(0, 1); lcd.print("###"); delay(1000);
            premios++;    premiosTot++;   banco -= pago;
            barrActiva  = LOW;            premioDetect = HIGH;
            mostrarDisplay();
            guardarUInt(DIR_PREMIOS,     premios);
            guardarUInt(DIR_PREMIOS_TOT, premiosTot);
            guardarInt (DIR_BANCO,       banco);
        }
    }
}

void probarBarrera() {
    if (tipoBarr == 1) {
        medirDistancia();
        if ((distAct + 130) < distRef || (distAct - 130) > distRef) barrActiva = HIGH;
        if (barrActiva == HIGH) { lcd.setCursor(0, 1); lcd.print("###"); delay(1000); }
    } else {
        if (digitalRead(PIN_BARR_IR) == HIGH) barrActiva = HIGH;
        if (barrActiva == HIGH) { simBarr = HIGH; lcd.setCursor(0, 1); lcd.print("###"); delay(1000); }
    }
}

// ============================================================================
// FUNCIÓN: BOTÓN CON DEBOUNCE
// ============================================================================
bool leerBoton(int pin, int msDebounce = 50) {
    if (digitalRead(pin) == LOW) {
        delay(msDebounce);
        if (digitalRead(pin) == LOW) {
            while (digitalRead(pin) == LOW) delay(10);
            delay(50); return true;
        }
    }
    return false;
}

// ============================================================================
// MENÚ: ajusta un valor numérico con botones SUBE/BAJA
// Retorna el valor final cuando se presiona MENU
// ============================================================================
int16_t ajustarValor(const char* titulo, int16_t valor, 
                     int16_t vMin, int16_t vMax, int16_t paso) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(titulo);

    while (digitalRead(PIN_BTN_MENU) == HIGH) {
        lcd.setCursor(0, 1); lcd.print("      ");
        lcd.setCursor(0, 1); lcd.print(valor);
        if (leerBoton(PIN_BTN_SUBE) && valor < vMax) valor += paso;
        if (leerBoton(PIN_BTN_BAJA) && valor > vMin) valor -= paso;
        delay(10);
    }
    while (digitalRead(PIN_BTN_MENU) == LOW) delay(20);
    delay(100);
    return valor;
}

// ============================================================================
// MENÚ: elige entre dos opciones con botones SUBE/BAJA
// opt0 y opt1 son los textos de cada opción
// Retorna 0 o 1 según la selección
// ============================================================================
int16_t elegirOpcion(const char* titulo, int16_t valorActual,
                     const char* opt0,   const char* opt1) {
    int16_t sel = valorActual;
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(titulo);
    lcd.setCursor(0, 1); lcd.print(sel == 0 ? opt0 : opt1);

    while (digitalRead(PIN_BTN_MENU) == HIGH) {
        if (leerBoton(PIN_BTN_SUBE) && sel != 0) {
            sel = 0;
            lcd.setCursor(0, 1); lcd.print("          ");
            lcd.setCursor(0, 1); lcd.print(opt0);
        }
        if (leerBoton(PIN_BTN_BAJA) && sel != 1) {
            sel = 1;
            lcd.setCursor(0, 1); lcd.print("          ");
            lcd.setCursor(0, 1); lcd.print(opt1);
        }
        delay(10);
    }
    while (digitalRead(PIN_BTN_MENU) == LOW) delay(20);
    delay(100);
    return sel;
}

// ============================================================================
// MENÚ: función principal de programación
// ============================================================================
void programar() {

    // --- Pantalla de versión ---
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("VERSION 1.7");
    lcd.setCursor(0, 1); lcd.print("24/5/24");
    delay(500);
    while (digitalRead(PIN_BTN_MENU) == LOW)  delay(20);
    delay(100);
    while (digitalRead(PIN_BTN_MENU) == HIGH) delay(20);
    while (digitalRead(PIN_BTN_MENU) == LOW)  delay(20);
    delay(100);

    // --- Totales acumulados (solo lectura) ---
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("PJ:"); lcd.setCursor(4, 0); lcd.print(jugadasTot);
    lcd.setCursor(0, 1); lcd.print("PP:"); lcd.setCursor(4, 1); lcd.print(premiosTot);
    while (digitalRead(PIN_BTN_MENU) == HIGH) delay(20);
    while (digitalRead(PIN_BTN_MENU) == LOW)  delay(20);
    delay(100);

    // --- Borrar contadores ---
    borrarCont = elegirOpcion("BORRA CONTADORES", LOW, "NO", "SI");
    if (borrarCont == HIGH) {
        guardarUInt(DIR_JUGADAS, 0); guardarUInt(DIR_PREMIOS, 0);
        guardarInt(DIR_BANCO, 0);
        jugadas = 0; premios = 0; banco = 0; borrarCont = LOW;
        lcd.clear(); lcd.setCursor(0, 0); lcd.print("BORRADOS"); delay(1000);
    }

    // --- Parámetros numéricos ---
    modoDisp = elegirOpcion("Display Modo",   modoDisp,  "Modo Contadores", "Modo Coin");
    pago     = ajustarValor("AJUSTAR PAGO",   pago,      1,    99,   1);
    tAgarre  = ajustarValor("AJUSTAR TIEMPO", tAgarre,   500,  5000, 10);
    tFuerte  = ajustarValor("T. FUERTE",      tFuerte,   0,    5000, 10);
    fuerza   = ajustarValor("AJUSTAR FUERZA", fuerza,    5,    101,  1);
    tipoBarr = elegirOpcion("TIPO BARRERA",   tipoBarr,  "INFRARROJO", "ULTRASONIDO");

    // --- Prueba barrera ---
    lcd.clear(); lcd.setCursor(0, 0); lcd.print("PRUEBA BARRERA");
    while (digitalRead(PIN_BTN_MENU) == HIGH) {
        lcd.setCursor(0, 1); lcd.print("        ");
        lcd.setCursor(0, 1); lcd.print(distAct);
        lcd.setCursor(8, 1); lcd.print(distRef);
        probarBarrera();
        if (barrActiva == HIGH) {
            lcd.setCursor(0, 1); lcd.print("###");
            delay(1000); barrActiva = LOW;
        }
        delay(50);
    }
    while (digitalRead(PIN_BTN_MENU) == LOW) delay(20);
    delay(100);

    // --- Guardar todo en EEPROM ---
    guardarInt(DIR_MODO_DISP,    modoDisp);
    guardarInt(DIR_PAGO,         pago);
    guardarInt(DIR_T_AGARRE,     tAgarre);
    guardarInt(DIR_T_FUERTE,     tFuerte);
    guardarInt(DIR_FUERZA,       fuerza);
    guardarInt(DIR_TIPO_BARRERA, tipoBarr);

    mostrarDisplay();
}

void moverPinza(bool conPremio) {

    if (conPremio) {
        analogWrite(PIN_MOTOR, 250);
        delay(2000);

    } else {
        analogWrite(PIN_MOTOR, 255); delay(tFuerte);

        fuerzaAdj = (banco <= -10) ? fuerza * 0.8 : fuerza;
        fuerzaVar = random(fuerzaAdj * 1.8, fuerzaAdj * 2.5);
        fuerzaDec = (fuerzaVar - fuerzaAdj) / 10;
        delayPaso = tAgarre / 10;

        for (pasoFuerza = 0; pasoFuerza <= 10; pasoFuerza++) {
            fuerzaVar -= fuerzaDec;
            analogWrite(PIN_MOTOR, fuerzaVar);
            delay(delayPaso);
        }

        analogWrite(PIN_MOTOR, fuerza * 1.3); delay(300);
        analogWrite(PIN_MOTOR, fuerza);       delay(100);
        analogWrite(PIN_MOTOR, fuerza * 1.3);
    }

    // Bucle de espera común
    bool esperando = true;
    while (esperando) {
        while (cntPinza < 3000) {
            clienteMQTT.loop();
            if (digitalRead(PIN_MOTOR) == HIGH) cntPinza = 0;
            if (cntPinza == 150)               analogWrite(PIN_MOTOR, 0);
            if (digitalRead(PIN_MOTOR) == LOW)  { cntPinza++; delay(1); }
            if (tBarrCheck <= 20) tBarrCheck++;
            if (tBarrCheck >= 19) {
                tBarrCheck = 0;
                if (premioDetect == LOW) leerBarrera();
            }
            if (cntPinza == 2998)     esperando = false;
            if (premioDetect == HIGH) { esperando = false; break; }
        }
    }
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);

    if (USAR_EEPROM_EXTERNA) {
        Wire.begin();
        Serial.println("MODO: EEPROM EXTERNA (I2C)");
    } else {
        EEPROM.begin(EEPROM_TAM);
        Wire.begin();
        Serial.println("MODO: EEPROM INTERNA (FLASH)");
    }

    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    analogWriteFrequency(PIN_MOTOR, 100);
    lcd.init(); lcd.backlight();

    pinMode(PIN_TRIGGER,     OUTPUT);
    pinMode(PIN_ECHO,        INPUT);
    pinMode(PIN_BARR_IR,     INPUT_PULLUP);
    pinMode(PIN_CREDITO,     OUTPUT);
    pinMode(PIN_BTN_MENU,    INPUT_PULLUP);
    pinMode(PIN_SENS_POS,    OUTPUT);
    pinMode(PIN_MOTOR,       INPUT_PULLUP);
    pinMode(PIN_LUZ,         OUTPUT);
    pinMode(PIN_BTN_SUBE,    INPUT_PULLUP);
    pinMode(PIN_BTN_BAJA,    INPUT_PULLUP);
    pinMode(PIN_SENS_PREMIO, INPUT_PULLUP);
    pinMode(PIN_MONEDA,      INPUT_PULLUP);

    // Inicializar EEPROM si es la primera vez
    inicio = leerInt(DIR_INICIO_CHECK);
    if (inicio != VAL_INICIO_CHECK) {
        Serial.println("Inicializando memoria...");
        guardarUInt(DIR_JUGADAS,     0); guardarUInt(DIR_PREMIOS,     0);
        guardarInt (DIR_BANCO,       0); guardarInt (DIR_PAGO,        12);
        guardarInt (DIR_T_AGARRE, 2000); guardarInt (DIR_FUERZA,      50);
        guardarUInt(DIR_JUGADAS_TOT, 0); guardarUInt(DIR_PREMIOS_TOT,  0);
        guardarInt (DIR_TIPO_BARRERA,0); guardarInt (DIR_MODO_DISP,    0);
        guardarInt (DIR_T_FUERTE,    0); guardarInt (DIR_INICIO_CHECK, VAL_INICIO_CHECK);
    }

    jugadas    = leerUInt(DIR_JUGADAS);
    premios    = leerUInt(DIR_PREMIOS);
    banco      = leerInt (DIR_BANCO);
    pago       = leerInt (DIR_PAGO);
    tAgarre    = leerInt (DIR_T_AGARRE);
    fuerza     = leerInt (DIR_FUERZA);
    jugadasTot = leerUInt(DIR_JUGADAS_TOT);
    premiosTot = leerUInt(DIR_PREMIOS_TOT);
    tipoBarr   = leerInt (DIR_TIPO_BARRERA);
    modoDisp   = leerInt (DIR_MODO_DISP);
    tFuerte    = leerInt (DIR_T_FUERTE);

    if (digitalRead(PIN_BTN_MENU) == LOW) {
        lcd.clear(); lcd.setCursor(0, 0); lcd.print("CONT.FIJOS");
        lcd.setCursor(0, 1); lcd.print("PJ:"); lcd.print(jugadasTot);
        lcd.print(" PP:"); lcd.print(premiosTot); delay(1000);
        while (digitalRead(PIN_BTN_MENU) == HIGH) delay(20);
    }
    delay(1000);

    // Calibrar distancia de referencia con N muestras
    for (int i = 0; i <= 10; i++) {
        medirDistancia();
        distRef = distAct;
        delay(50);
    }

    digitalWrite(PIN_LUZ, 1);

    conectarWifi();
    clienteMQTT.setServer(MQTT_SERVER, MQTT_PUERTO);
    tickerPulso.attach(60, activarPulso);
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!clienteMQTT.connected()) reconectarMQTT();
        clienteMQTT.loop();
    }

    if (flagPulso) { enviarHeartbeat(); flagPulso = false; }

    mostrarDisplay();

    // -------------------------------------------------------------------------
    // BUCLE DE ESPERA (máquina idle, esperando que el jugador pulse)
    // -------------------------------------------------------------------------
    while (digitalRead(PIN_MOTOR) == LOW && debPinza < 5) {
        clienteMQTT.loop();
        if (flagPulso) { enviarHeartbeat(); flagPulso = false; }

        cntEspera++;
        if (digitalRead(PIN_MOTOR) == HIGH) debPinza++;
        if (digitalRead(PIN_MOTOR) == LOW)  debPinza = 0;
        if (cntEspera < 100000) cntEspera++;

        leerMoneda();

        if (jugadasTot != prevJugadasTot || premiosTot != prevPremiosTot || banco != prevBanco) {
            enviarDatos();
            prevJugadasTot = jugadasTot;
            prevPremiosTot = premiosTot;
            prevBanco      = banco;
        }

        if (digitalRead(PIN_BTN_MENU) == LOW) cntMenuBtn++;
        else cntMenuBtn = 0;
        if (digitalRead(PIN_BTN_MENU) == LOW && cntMenuBtn == 30) {
            tickerPulso.detach();
            programar();
            tickerPulso.attach(60, activarPulso);
            cntMenuBtn = 0;
        }

        if (cntBanco >= 18000 && tBanco > 0) { cntBanco = 0; tBanco--; }

        if (tBarrCheck < 200) {
            tBarrCheck++;
            if (tBarrCheck == 199) {
                tBarrCheck = 0;
                if (premioDetect == LOW) leerBarrera();
            }
        }

        if (WiFi.status() == WL_CONNECTED) {
            if (!clienteMQTT.connected()) reconectarMQTT();
            clienteMQTT.loop();
        }
    }

    // -------------------------------------------------------------------------
    // SECUENCIA DE JUEGO
    // -------------------------------------------------------------------------
    premioDetect = LOW; debPinza = 0; barrActiva = LOW;
    if (creditos >= 1) creditos--;
    mostrarDisplay();

    jugadas++; banco++; jugadasTot++;
    if (tBanco < 10) tBanco++;

    guardarInt (DIR_BANCO,       banco);
    guardarUInt(DIR_JUGADAS_TOT, jugadasTot);
    guardarUInt(DIR_JUGADAS,     jugadas);

    bool hayPremio = (pago <= banco && random(5) <= 3);
    moverPinza(hayPremio);

    analogWrite(PIN_MOTOR, 0);
    tBarrCheck = 0; cntPinza = 0; debPinza = 0;
}
