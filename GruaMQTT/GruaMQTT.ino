// Gigga — Grúa con configuración remota por MQTT (panel AdministrationPanel)
// Marzo 2026
// Protocolo: MQTT
// Memoria: Seleccionable (Interna ESP32 o Externa I2C)
//
// Cambios vs versión base:
// - Tópico config + ota: JSON del panel (publish_grua_config.php / publish_ota_update.php)
// - Tópicos armados desde ID_DISP + prefijo maquinas (alineado a MQTT_TOPIC_PREFIX del servidor)
// - setCallback + subscribe tras cada connect MQTT
// - aplicarConfigRemota: valida rangos, EEPROM + RAM
// - OTA HTTPS: cola por MQTT, ejecución en bucle de espera y al fin de loop()
//
// ==========================================
// 🎚️ CONFIGURACIÓN DE MEMORIA (TOCA ACA)
// ==========================================
#define USAR_EEPROM_EXTERNA 0

// 0 = MQTT por WiFiClient (ej. 1883); 1 = WiFiClientSecure + setInsecure (ej. 8883)
#ifndef MQTT_USAR_TLS
#define MQTT_USAR_TLS 0
#endif

#ifndef FW_VERSION
#define FW_VERSION "0.0.0-local"
#endif

#include <Wire.h>
// LCD I2C: usá una variante compatible con ESP32 (no la que indica solo AVR).
// Recomendado: "LiquidCrystal I2C" de Frank de Brabander, o fork johnrickman (GitHub).
#include <LiquidCrystal_I2C.h>
#include <Arduino.h>
#include <WiFi.h>
// Siempre: OTA HTTPS usa WiFiClientSecure. MQTT puede usar WiFiClient o Secure según MQTT_USAR_TLS.
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Ticker.h>
#include <EEPROM.h>
#include "mbedtls/sha256.h"

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
#define PIN_TRIGGER      13
#define PIN_ECHO         12
#define PIN_LUZ          19
#define PIN_BARR_IR      14
#define PIN_MOTOR        16
#define PIN_SENS_PINZA   17
#define PIN_BTN_BAJA     35
#define PIN_SENS_PREMIO  27
#define PIN_BTN_SUBE     34
#define PIN_BTN_MENU      4
#define PIN_MONEDA       26
#define PIN_CREDITO      25

// ============================================================================
// WIFI Y MQTT (ID_DISP = codigo_hardware en BD)
// ============================================================================
const char* ID_DISP     = "Grua_123";
const char* WIFI_SSID   = "FIBRA-WIFI6-229F";
const char* WIFI_CLAVE  = "46332714";
const char* MQTT_SERVER = "broker.emqx.io";
const int   MQTT_PUERTO = 1883;

#define MQTT_PREFIJO        "maquinas"
#define MQTT_SUB_DATOS      "datos"
#define MQTT_SUB_HEARTBEAT  "heartbeat"
#define MQTT_SUB_CONFIG     "config"
#define MQTT_SUB_OTA        "ota"

char topic_datos[96];
char topic_heartbeat[96];
char topic_config[96];
char topic_ota[96];

const char* DNI_ADMIN   = "00000000";

// Mismo valor que OTA_SHARED_SECRET del panel si aplica; cadena vacía = no validar ota_secret
static const char OTA_SHARED_SECRET_STR[] = "";

// ============================================================================
// OBJETOS
// ============================================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);
#if MQTT_USAR_TLS
WiFiClientSecure clienteWifi;
#else
WiFiClient   clienteWifi;
#endif
PubSubClient clienteMQTT(clienteWifi);
Ticker       tickerPulso;

// ============================================================================
// OTA (cola; se ejecuta en espera y entre ciclos)
// =======================================================================a=====
volatile bool otaPendiente = false;
char otaPendienteUrl[256];
char otaPendienteSha256[65];

// ============================================================================
// VARIABLES: CONTADORES Y BALANCE
// ============================================================================
unsigned int jugadas     = 0;
unsigned int premios     = 0;
int16_t      banco       = 0;
unsigned int tBanco      = 0;
unsigned int jugadasTot  = 0;
unsigned int premiosTot  = 0;

// ============================================================================
// VARIABLES: CONFIGURACIÓN DE JUEGO
// ============================================================================
int16_t pago        = 20;
int16_t tAgarre     = 0;
int16_t fuerza      = 50;
int16_t tFuerte     = 0;
int16_t modoDisp    = 0;
int16_t tipoBarr    = 0;

// ============================================================================
// VARIABLES: SENSOR DE DISTANCIA
// ============================================================================
int distRef  = 0;
int distAct  = 0;
int tEco     = 0;

// ============================================================================
// VARIABLES: BARRERA Y PREMIO
// ============================================================================
int barrActiva   = 0;
int premioDetect = 0;

// ============================================================================
// VARIABLES: MONEDA Y CRÉDITO
// ============================================================================
int creditos   = 0;
int debMonSube = 0;
int debMonBaja = 0;

// ============================================================================
// VARIABLES: PINZA Y FUERZA
// ============================================================================
float fuerzaVar = 0;
float fuerzaDec = 0;
int   fuerzaAdj = 0;

// ============================================================================
// VARIABLES: TEMPORIZACIÓN Y BUCLES
// ============================================================================
unsigned long cntPinza   = 0;
int           debPinza   = 0;
int           pasoFuerza = 0;
int           delayPaso  = 0;
unsigned long tBarrCheck = 0;
int           cntBanco   = 0;
int16_t       inicio     = 0;
int           cntMenuBtn = 0;
int           simBarr    = 0;
int           borrarCont = LOW;

// ============================================================================
// VARIABLES: MQTT Y WIFI
// ============================================================================
volatile bool flagPulso    = false;
unsigned long tUltReconMQTT = 0;
unsigned int prevJugadasTot = 0;
unsigned int prevPremiosTot = 0;
int prevBanco              = 0;

// ============================================================================
// PROTOTIPOS
// ============================================================================
void construirTopicsMqtt();
void aplicarConfigRemota(JsonDocument& doc);
void onMqttMessage(char* topic, byte* payload, unsigned int length);
bool encolarOtaDesdeJson(JsonDocument& doc);
void ejecutarOtaSiPendiente();
bool descargarYFlashearOta(const char* url, const char* sha256Hex);
void activarPulso();
void mostrarDisplay();

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
// FUNCIONES: EEPROM ALTO NIVEL
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
// TÓPICOS MQTT
// ============================================================================
void construirTopicsMqtt() {
    snprintf(topic_datos, sizeof(topic_datos), "%s/%s/%s",
             MQTT_PREFIJO, ID_DISP, MQTT_SUB_DATOS);
    snprintf(topic_heartbeat, sizeof(topic_heartbeat), "%s/%s/%s",
             MQTT_PREFIJO, ID_DISP, MQTT_SUB_HEARTBEAT);
    snprintf(topic_config, sizeof(topic_config), "%s/%s/%s",
             MQTT_PREFIJO, ID_DISP, MQTT_SUB_CONFIG);
    snprintf(topic_ota, sizeof(topic_ota), "%s/%s/%s",
             MQTT_PREFIJO, ID_DISP, MQTT_SUB_OTA);
}

// ============================================================================
// OTA: utilidades
// ============================================================================
static bool hexNibbleOta(char c, uint8_t* out) {
    if (c >= '0' && c <= '9') { *out = (uint8_t)(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { *out = (uint8_t)(10 + c - 'a'); return true; }
    if (c >= 'A' && c <= 'F') { *out = (uint8_t)(10 + c - 'A'); return true; }
    return false;
}

static bool parseSha256HexOta(const char* hex, uint8_t out32[32]) {
    if (strlen(hex) != 64) return false;
    for (int i = 0; i < 32; i++) {
        uint8_t hi, lo;
        if (!hexNibbleOta(hex[i * 2], &hi) || !hexNibbleOta(hex[i * 2 + 1], &lo)) return false;
        out32[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool sha256IgualesOta(const uint8_t a[32], const uint8_t b[32]) {
    for (int i = 0; i < 32; i++)
        if (a[i] != b[i]) return false;
    return true;
}

static bool urlEsHttps(const char* url) {
    return url && strncmp(url, "https://", 8) == 0;
}

bool encolarOtaDesdeJson(JsonDocument& doc) {
    const char* url = doc["url"];
    const char* sha = doc["sha256"];
    const char* ver = doc["version"];
    if (!url || !sha || !ver || !ver[0]) return false;
    if (!urlEsHttps(url)) return false;
    if (strlen(sha) != 64) return false;
    uint8_t tmp[32];
    if (!parseSha256HexOta(sha, tmp)) return false;

    if (OTA_SHARED_SECRET_STR[0] != '\0') {
        const char* sec = doc["ota_secret"].as<const char*>();
        if (!sec) sec = "";
        if (strcmp(sec, OTA_SHARED_SECRET_STR) != 0) {
            Serial.println("OTA rechazada: ota_secret inválido o ausente");
            return false;
        }
    }

    strncpy(otaPendienteUrl, url, sizeof(otaPendienteUrl) - 1);
    otaPendienteUrl[sizeof(otaPendienteUrl) - 1] = '\0';
    strncpy(otaPendienteSha256, sha, sizeof(otaPendienteSha256) - 1);
    otaPendienteSha256[sizeof(otaPendienteSha256) - 1] = '\0';
    otaPendiente = true;
    Serial.printf("OTA encolada version=%s\n", ver);
    return true;
}

bool descargarYFlashearOta(const char* url, const char* sha256Hex) {
    uint8_t esperado[32];
    if (!parseSha256HexOta(sha256Hex, esperado)) {
        Serial.println("OTA: sha256 inválido");
        return false;
    }

    WiFiClientSecure cli;
    cli.setInsecure();
    HTTPClient http;
    // setTimeout en ESP32 suele ser uint16_t ms (máx. ~65 s)
    http.setTimeout(60000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(cli, url)) {
        Serial.println("OTA: begin falló");
        return false;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("OTA: HTTP %d\n", code);
        http.end();
        return false;
    }

    int contentLen = http.getSize();
    WiFiClient* stream = http.getStreamPtr();
    mbedtls_sha256_context shaCtx;
    mbedtls_sha256_init(&shaCtx);
    mbedtls_sha256_starts(&shaCtx, 0);

    bool okBegin = (contentLen > 0)
                       ? Update.begin((size_t)contentLen, U_FLASH)
                       : Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
    if (!okBegin) {
        Serial.println("OTA: Update.begin falló");
        mbedtls_sha256_free(&shaCtx);
        http.end();
        return false;
    }

    uint8_t buff[512];
    size_t total = 0;
    while (true) {
        if (contentLen > 0 && total >= (size_t)contentLen) break;
        if (!http.connected() && !stream->available()) break;
        size_t n = stream->available();
        if (!n) {
            delay(1);
            continue;
        }
        if (contentLen > 0) {
            size_t rest = (size_t)contentLen - total;
            if (n > rest) n = rest;
        }
        if (n > sizeof(buff)) n = sizeof(buff);
        int r = stream->readBytes(buff, n);
        if (r <= 0) break;
        mbedtls_sha256_update(&shaCtx, buff, (size_t)r);
        if (Update.write(buff, (size_t)r) != (size_t)r) {
            Serial.println("OTA: escritura falló");
            mbedtls_sha256_free(&shaCtx);
            Update.abort();
            http.end();
            return false;
        }
        total += (size_t)r;
    }

    uint8_t hash[32];
    mbedtls_sha256_finish(&shaCtx, hash);
    mbedtls_sha256_free(&shaCtx);
    http.end();

    if (!sha256IgualesOta(hash, esperado)) {
        Serial.println("OTA: SHA256 no coincide");
        Update.abort();
        return false;
    }
    if (contentLen > 0 && total != (size_t)contentLen) {
        Serial.println("OTA: tamaño incompleto");
        Update.abort();
        return false;
    }

    if (!Update.end(true)) {
        Serial.printf("OTA: Update.end error=%u\n", (unsigned)Update.getError());
        return false;
    }
    Serial.println("OTA OK, reinicio");
    delay(300);
    ESP.restart();
    return true;
}

void ejecutarOtaSiPendiente() {
    if (!otaPendiente) return;
    if (WiFi.status() != WL_CONNECTED) return;

    char url[256];
    char sha[65];
    strncpy(url, otaPendienteUrl, sizeof(url) - 1);
    strncpy(sha, otaPendienteSha256, sizeof(sha) - 1);
    url[sizeof(url) - 1] = '\0';
    sha[sizeof(sha) - 1] = '\0';

    otaPendiente = false;
    tickerPulso.detach();
    bool ok = descargarYFlashearOta(url, sha);
    tickerPulso.attach(600, activarPulso);
    if (!ok) Serial.println("OTA: reintentá desde el panel");
}

// ============================================================================
// CONFIG REMOTA (MQTT) — mismo contrato que publish_grua_config.php
// ============================================================================
void aplicarConfigRemota(JsonDocument& doc) {
    if (!doc["pago"].is<int>() || !doc["t_agarre"].is<int>() ||
        !doc["t_fuerte"].is<int>() || !doc["fuerza"].is<int>()) {
        Serial.println("Config remota: faltan campos numéricos");
        return;
    }
    int pg = doc["pago"].as<int>();
    int ta = doc["t_agarre"].as<int>();
    int tf = doc["t_fuerte"].as<int>();
    int fz = doc["fuerza"].as<int>();

    if (pg < 1 || pg > 99)   { Serial.println("Config remota: pago fuera de rango"); return; }
    if (ta < 500 || ta > 5000) { Serial.println("Config remota: t_agarre fuera de rango"); return; }
    if (tf < 0 || tf > 5000) { Serial.println("Config remota: t_fuerte fuera de rango"); return; }
    if (fz < 5 || fz > 101)  { Serial.println("Config remota: fuerza fuera de rango"); return; }

    pago    = (int16_t)pg;
    tAgarre = (int16_t)ta;
    tFuerte = (int16_t)tf;
    fuerza  = (int16_t)fz;

    guardarInt(DIR_PAGO,     pago);
    guardarInt(DIR_T_AGARRE, tAgarre);
    guardarInt(DIR_T_FUERTE, tFuerte);
    guardarInt(DIR_FUERZA,   fuerza);

    Serial.printf("Config remota OK: PA=%d TA=%d TF=%d FZ=%d\n", pago, tAgarre, tFuerte, fuerza);
    mostrarDisplay();
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    if (length == 0 || length >= 512) return;

    char buf[512];
    memcpy(buf, payload, length);
    buf[length] = '\0';

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, buf);
    if (err) {
        Serial.print("MQTT JSON error: ");
        Serial.println(err.c_str());
        return;
    }
    const char* cmd = doc["cmd"];
    if (!cmd) return;

    if (strcmp(topic, topic_config) == 0) {
        if (strcmp(cmd, "set_grua_params") != 0) return;
        aplicarConfigRemota(doc);
        return;
    }
    if (strcmp(topic, topic_ota) == 0) {
        if (strcmp(cmd, "ota_update") != 0) return;
        encolarOtaDesdeJson(doc);
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
    if (clienteMQTT.connect(ID_DISP, "maquinas/status", 1, true, "0")) {
        Serial.println("OK");
        clienteMQTT.publish("maquinas/status", "1");
        if (clienteMQTT.subscribe(topic_config, 1)) {
            Serial.print("Subscribe "); Serial.print(topic_config); Serial.println(" OK");
        } else {
            Serial.println("Subscribe config falló");
        }
        if (clienteMQTT.subscribe(topic_ota, 1)) {
            Serial.print("Subscribe "); Serial.print(topic_ota); Serial.println(" OK");
        } else {
            Serial.println("Subscribe ota falló");
        }
    } else {
        Serial.print("Error rc="); Serial.println(clienteMQTT.state());
    }
}

void enviarDatos() {
    if (!clienteMQTT.connected()) return;
    JsonDocument doc;
    doc["action"]          = 2;
    doc["dni_admin"]       = DNI_ADMIN;
    doc["codigo_hardware"] = ID_DISP;
    doc["tipo_maquina"]    = 2;
    doc["payload"]["pago"]   = pago;
    doc["payload"]["coin"]   = jugadasTot;
    doc["payload"]["premios"]= premiosTot;
    doc["payload"]["banco"] = banco;
    char buf[320];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return;
    if (clienteMQTT.publish(topic_datos, buf, len)) Serial.println("Datos enviados MQTT");
}

void activarPulso() { flagPulso = true; }

void enviarHeartbeat() {
  if (!clienteMQTT.connected()) return;
  static char buf[160];
  JsonDocument doc;
  doc["action"] = 1;
  doc["dni_admin"] = DNI_ADMIN;
  doc["codigo_hardware"] = ID_DISP;
  doc["tipo_maquina"] = 2;
  doc["ts"] = millis();
  size_t len = serializeJson(doc, buf, sizeof(buf));
  if (len >= sizeof(buf)) return;
  buf[len] = '\0';
  if (clienteMQTT.publish(topic_heartbeat, buf, len)) Serial.println("Heartbeat enviado");
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
// MENÚ
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

void programar() {

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("VERSION "); lcd.print(FW_VERSION);
    lcd.setCursor(0, 1); lcd.print("MQTT 21/3/26");
    delay(500);
    while (digitalRead(PIN_BTN_MENU) == LOW)  delay(20);
    delay(100);
    while (digitalRead(PIN_BTN_MENU) == HIGH) delay(20);
    while (digitalRead(PIN_BTN_MENU) == LOW)  delay(20);
    delay(100);

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("PJ:"); lcd.setCursor(4, 0); lcd.print(jugadasTot);
    lcd.setCursor(0, 1); lcd.print("PP:"); lcd.setCursor(4, 1); lcd.print(premiosTot);
    while (digitalRead(PIN_BTN_MENU) == HIGH) delay(20);
    while (digitalRead(PIN_BTN_MENU) == LOW)  delay(20);
    delay(100);

    borrarCont = elegirOpcion("BORRA CONTADORES", LOW, "NO", "SI");
    if (borrarCont == HIGH) {
        guardarUInt(DIR_JUGADAS, 0); guardarUInt(DIR_PREMIOS, 0);
        guardarInt(DIR_BANCO, 0);
        jugadas = 0; premios = 0; banco = 0; borrarCont = LOW;
        lcd.clear(); lcd.setCursor(0, 0); lcd.print("BORRADOS"); delay(1000);
    }

    modoDisp = elegirOpcion("Display Modo",   modoDisp,  "Modo Contadores", "Modo Coin");
    pago     = ajustarValor("AJUSTAR PAGO",   pago,      1,    99,   1);
    tAgarre  = ajustarValor("AJUSTAR TIEMPO", tAgarre,   500,  5000, 10);
    tFuerte  = ajustarValor("T. FUERTE",      tFuerte,   0,    5000, 10);
    fuerza   = ajustarValor("AJUSTAR FUERZA", fuerza,    5,    101,  1);
    tipoBarr = elegirOpcion("TIPO BARRERA",   tipoBarr,  "INFRARROJO", "ULTRASONIDO");

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

    bool esperando = true;
    while (esperando) {
        cntPinza = 0;
        while (cntPinza < 3000) {
            clienteMQTT.loop();
            if (digitalRead(PIN_SENS_PINZA) == HIGH) cntPinza = 0;
            if (cntPinza == 150) analogWrite(PIN_MOTOR, 0);
            if (digitalRead(PIN_SENS_PINZA) == LOW)  { cntPinza++; delay(1); }
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
    pinMode(PIN_MOTOR,       OUTPUT);
    pinMode(PIN_SENS_PINZA,  INPUT_PULLUP);
    pinMode(PIN_LUZ,         OUTPUT);
    pinMode(PIN_BTN_SUBE,    INPUT_PULLUP);
    pinMode(PIN_BTN_BAJA,    INPUT_PULLUP);
    pinMode(PIN_SENS_PREMIO, INPUT_PULLUP);
    pinMode(PIN_MONEDA,      INPUT_PULLUP);

    inicio = leerInt(DIR_INICIO_CHECK);
    if (inicio != VAL_INICIO_CHECK) {
        Serial.println("Inicializando memoria...");
        guardarUInt(DIR_JUGADAS,     0); guardarUInt(DIR_PREMIOS,     0);
        guardarInt (DIR_BANCO,       0); guardarInt (DIR_PAGO,        12);
        guardarInt (DIR_T_AGARRE, 2000); guardarInt (DIR_FUERZA,       50);
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

    for (int i = 0; i <= 10; i++) {
        medirDistancia();
        distRef = distAct;
        delay(50);
    }

    digitalWrite(PIN_LUZ, 1);

    construirTopicsMqtt();
    conectarWifi();
#if MQTT_USAR_TLS
    clienteWifi.setInsecure();
#endif
    clienteMQTT.setServer(MQTT_SERVER, MQTT_PUERTO);
    clienteMQTT.setBufferSize(2048);
    clienteMQTT.setCallback(onMqttMessage);
    tickerPulso.attach(600, activarPulso);
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!clienteMQTT.connected()) reconectarMQTT();
        clienteMQTT.loop();
    }

    if (flagPulso) { enviarHeartbeat(); flagPulso = false; }

    mostrarDisplay();

    while (digitalRead(PIN_SENS_PINZA) == LOW && debPinza < 5) {
        clienteMQTT.loop();
        if (flagPulso) { enviarHeartbeat(); flagPulso = false; }
        ejecutarOtaSiPendiente();

        if (digitalRead(PIN_SENS_PINZA) == HIGH) debPinza++;
        if (digitalRead(PIN_SENS_PINZA) == LOW)  debPinza = 0;

        cntBanco++;
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
            tickerPulso.attach(600, activarPulso);
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
    ejecutarOtaSiPendiente();
}
