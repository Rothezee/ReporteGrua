// MqttOnly — Telemetria MQTT + OTA remota (sin logica de grua)
// Marzo 2026

#ifndef MQTT_USAR_TLS
#define MQTT_USAR_TLS 0
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Ticker.h>
#include "mbedtls/sha256.h"

const char* ID_DISP = "Grua_123";
const char* WIFI_SSID = "FIBRA-WIFI6-229F";
const char* WIFI_CLAVE = "46332714";
const char* MQTT_SERVER = "broker.emqx.io";
const int MQTT_PUERTO = 1883;
const char* DNI_ADMIN = "00000000";

#define MQTT_PREFIJO "maquinas"
#define MQTT_SUB_DATOS "datos"
#define MQTT_SUB_HEARTBEAT "heartbeat"
#define MQTT_SUB_CONFIG "config"
#define MQTT_SUB_OTA "ota"

static const char OTA_SHARED_SECRET_STR[] = "";

char topic_datos[96];
char topic_heartbeat[96];
char topic_config[96];
char topic_ota[96];

#if MQTT_USAR_TLS
WiFiClientSecure clienteWifi;
#else
WiFiClient clienteWifi;
#endif
PubSubClient clienteMQTT(clienteWifi);
Ticker tickerPulso;
Ticker tickerDatos;

volatile bool flagPulso = false;
volatile bool flagDatos = false;
volatile bool otaPendiente = false;
char otaPendienteUrl[256];
char otaPendienteSha256[65];
unsigned long tUltReconMQTT = 0;

unsigned int jugadasTot = 0;
unsigned int premiosTot = 0;
int16_t banco = 0;
int16_t pago = 20;

void construirTopicsMqtt() {
  snprintf(topic_datos, sizeof(topic_datos), "%s/%s/%s", MQTT_PREFIJO, ID_DISP, MQTT_SUB_DATOS);
  snprintf(topic_heartbeat, sizeof(topic_heartbeat), "%s/%s/%s", MQTT_PREFIJO, ID_DISP, MQTT_SUB_HEARTBEAT);
  snprintf(topic_config, sizeof(topic_config), "%s/%s/%s", MQTT_PREFIJO, ID_DISP, MQTT_SUB_CONFIG);
  snprintf(topic_ota, sizeof(topic_ota), "%s/%s/%s", MQTT_PREFIJO, ID_DISP, MQTT_SUB_OTA);
}

void conectarWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.disconnect(true);
  delay(500);
  Serial.println("Conectando a WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_CLAVE);
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(500);
    Serial.print('.');
    intentos++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK");
    Serial.println(WiFi.localIP());
  }
}

void activarPulso() { flagPulso = true; }
void activarDatos() { flagDatos = true; }

void enviarHeartbeat() {
  if (!clienteMQTT.connected()) return;
  StaticJsonDocument<192> doc;
  doc["action"] = 1;
  doc["dni_admin"] = DNI_ADMIN;
  doc["codigo_hardware"] = ID_DISP;
  doc["tipo_maquina"] = 2;
  doc["ts"] = millis();
  char buf[192];
  size_t len = serializeJson(doc, buf, sizeof(buf));
  if (len > 0 && len < sizeof(buf)) clienteMQTT.publish(topic_heartbeat, buf, len);
}

void enviarDatos() {
  if (!clienteMQTT.connected()) return;
  jugadasTot++;
  StaticJsonDocument<320> doc;
  doc["action"] = 2;
  doc["dni_admin"] = DNI_ADMIN;
  doc["codigo_hardware"] = ID_DISP;
  doc["tipo_maquina"] = 2;
  JsonObject payload = doc["payload"].to<JsonObject>();
  payload["pago"] = pago;
  payload["coin"] = jugadasTot;
  payload["premios"] = premiosTot;
  payload["banco"] = banco;
  char buf[320];
  size_t len = serializeJson(doc, buf, sizeof(buf));
  if (len > 0 && len < sizeof(buf)) clienteMQTT.publish(topic_datos, buf, len);
}

static bool hexNibbleOta(char c, uint8_t* out) {
  if (c >= '0' && c <= '9') { *out = (uint8_t)(c - '0'); return true; }
  if (c >= 'a' && c <= 'f') { *out = (uint8_t)(10 + c - 'a'); return true; }
  if (c >= 'A' && c <= 'F') { *out = (uint8_t)(10 + c - 'A'); return true; }
  return false;
}

static bool parseSha256HexOta(const char* hex, uint8_t out32[32]) {
  if (!hex || strlen(hex) != 64) return false;
  for (int i = 0; i < 32; i++) {
    uint8_t hi, lo;
    if (!hexNibbleOta(hex[i * 2], &hi) || !hexNibbleOta(hex[i * 2 + 1], &lo)) return false;
    out32[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

static bool sha256IgualesOta(const uint8_t a[32], const uint8_t b[32]) {
  for (int i = 0; i < 32; i++) if (a[i] != b[i]) return false;
  return true;
}

bool descargarYFlashearOta(const char* url, const char* sha256Hex) {
  uint8_t esperado[32];
  if (!parseSha256HexOta(sha256Hex, esperado)) return false;

  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  http.setTimeout(60000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(cli, url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }

  int contentLen = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  mbedtls_sha256_context shaCtx;
  mbedtls_sha256_init(&shaCtx);
  mbedtls_sha256_starts(&shaCtx, 0);

  bool okBegin = (contentLen > 0) ? Update.begin((size_t)contentLen, U_FLASH) : Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
  if (!okBegin) { mbedtls_sha256_free(&shaCtx); http.end(); return false; }

  uint8_t buff[512];
  size_t total = 0;
  while (true) {
    if (contentLen > 0 && total >= (size_t)contentLen) break;
    if (!http.connected() && !stream->available()) break;
    size_t n = stream->available();
    if (!n) { delay(1); continue; }
    if (contentLen > 0) {
      size_t rest = (size_t)contentLen - total;
      if (n > rest) n = rest;
    }
    if (n > sizeof(buff)) n = sizeof(buff);
    int r = stream->readBytes(buff, n);
    if (r <= 0) break;
    mbedtls_sha256_update(&shaCtx, buff, (size_t)r);
    if (Update.write(buff, (size_t)r) != (size_t)r) {
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

  if (!sha256IgualesOta(hash, esperado)) { Update.abort(); return false; }
  if (contentLen > 0 && total != (size_t)contentLen) { Update.abort(); return false; }
  if (!Update.end(true)) return false;

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
  tickerDatos.detach();
  bool ok = descargarYFlashearOta(url, sha);
  tickerPulso.attach(60, activarPulso);
  tickerDatos.attach(120, activarDatos);
  if (!ok) Serial.println("OTA fallida");
}

void aplicarConfigRemota(JsonDocument& doc) {
  if (!doc["pago"].is<int>()) return;
  int pg = doc["pago"].as<int>();
  if (pg >= 1 && pg <= 99) pago = (int16_t)pg;
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (length == 0 || length >= 512) return;
  char buf[512];
  memcpy(buf, payload, length);
  buf[length] = '\0';

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, buf)) return;
  const char* cmd = doc["cmd"];
  if (!cmd) return;

  if (strcmp(topic, topic_config) == 0 && strcmp(cmd, "set_grua_params") == 0) {
    aplicarConfigRemota(doc);
    return;
  }

  if (strcmp(topic, topic_ota) == 0 && strcmp(cmd, "ota_update") == 0) {
    const char* url = doc["url"];
    const char* sha = doc["sha256"];
    const char* ver = doc["version"];
    if (!url || !sha || !ver || !ver[0]) return;
    if (strncmp(url, "https://", 8) != 0) return;
    uint8_t tmp[32];
    if (!parseSha256HexOta(sha, tmp)) return;
    if (OTA_SHARED_SECRET_STR[0] != '\0') {
      const char* sec = doc["ota_secret"] | "";
      if (strcmp(sec, OTA_SHARED_SECRET_STR) != 0) return;
    }
    strncpy(otaPendienteUrl, url, sizeof(otaPendienteUrl) - 1);
    strncpy(otaPendienteSha256, sha, sizeof(otaPendienteSha256) - 1);
    otaPendienteUrl[sizeof(otaPendienteUrl) - 1] = '\0';
    otaPendienteSha256[sizeof(otaPendienteSha256) - 1] = '\0';
    otaPendiente = true;
  }
}

void reconectarMQTT() {
  if (millis() - tUltReconMQTT < 5000) return;
  tUltReconMQTT = millis();
  if (clienteMQTT.connect(ID_DISP, "maquinas/status", 1, true, "0")) {
    clienteMQTT.publish("maquinas/status", "1");
    clienteMQTT.subscribe(topic_config, 1);
    clienteMQTT.subscribe(topic_ota, 1);
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  construirTopicsMqtt();
  conectarWifi();

#if MQTT_USAR_TLS
  clienteWifi.setInsecure();
#endif
  clienteMQTT.setServer(MQTT_SERVER, MQTT_PUERTO);
  clienteMQTT.setBufferSize(2048);
  clienteMQTT.setCallback(onMqttMessage);

  tickerPulso.attach(60, activarPulso);
  tickerDatos.attach(120, activarDatos);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) conectarWifi();

  if (WiFi.status() == WL_CONNECTED) {
    if (!clienteMQTT.connected()) reconectarMQTT();
    clienteMQTT.loop();
  }

  if (flagPulso) {
    enviarHeartbeat();
    flagPulso = false;
  }

  if (flagDatos) {
    enviarDatos();
    flagDatos = false;
  }

  ejecutarOtaSiPendiente();
  delay(5);
}
