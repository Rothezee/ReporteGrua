# Handoff para agente / desarrollador — repo **Rothezee/ReporteGrua**

Usá este documento como **contexto único** al abrir el repositorio del firmware en otro Cursor chat, clon local, o agente.

## Relación con AdministrationPanel (panel PHP)

| Lado | Repo / rol |
|------|------------|
| **Panel** | `AdministrationPanel` (PHP): dashboard, `publish_grua_config.php`, `publish_ota_update.php`, `list_ota_branches.php`, `OtaManifestHelper.php` |
| **Firmware** | **Este repo** [Rothezee/ReporteGrua](https://github.com/Rothezee/ReporteGrua): sketch [`GruaMQTT/GruaMQTT.ino`](GruaMQTT/GruaMQTT.ino), CI, [`ota/manifest.json`](ota/manifest.json) |

El panel **no** incluye ya una copia local del firmware: el código fuente y el CI viven **solo** en ReporteGrua.

## Contratos MQTT (deben coincidir con el panel)

- **Prefijo:** `maquinas` (env `MQTT_TOPIC_PREFIX` en el servidor, default `maquinas`).
- **Datos:** `maquinas/<codigo_hardware>/datos` — JSON formato API (`action`, `dni_admin`, `codigo_hardware`, `tipo_maquina`, `payload`, …).
- **Heartbeat:** `maquinas/<id>/heartbeat`.
- **Config remota (grúa):** `maquinas/<id>/config` — JSON: `cmd: "set_grua_params"`, `pago` (1–99), `t_agarre` (500–5000), `t_fuerte` (0–5000), `fuerza` (5–101), `ts`.
- **OTA:** `maquinas/<id>/ota` — JSON: `cmd: "ota_update"`, `url` (solo `https://`), `version`, `sha256` (64 hex), `ts`, opcional `branch`, opcional `ota_secret` si el servidor define `OTA_SHARED_SECRET` (en firmware: constante `OTA_SHARED_SECRET_STR`, misma cadena).

El `mqtt_listener.php` del panel **ignora** `.../config` y `.../ota` (no van a `api_receptor`).

## OTA desde GitHub (flujo)

1. **GitHub Actions** compila `GruaMQTT.ino` por rama y sube `firmware-<rama>.bin` a un release fijo (ej. tag `firmware-builds`).
2. En la rama índice (ej. `main`) existe **`ota/manifest.json`** con:
   ```json
   {
     "branches": {
       "main": { "version": "x.y.z", "url": "https://github.com/.../releases/download/.../firmware-main.bin", "sha256": "64_hex" },
       "mqtt-only": { ... }
     }
   }
   ```
3. El panel descarga el manifiesto desde `OTA_MANIFEST_URL` o  
   `https://raw.githubusercontent.com/Rothezee/ReporteGrua/<OTA_MANIFEST_REF>/ota/manifest.json`  
   (env: `GITHUB_REPO`, `OTA_MANIFEST_REF`, opcional `GITHUB_TOKEN`, `OTA_ALLOWED_BRANCHES`).

## Checklist en ReporteGrua

- [x] **`GruaMQTT/GruaMQTT.ino`**: `ID_DISP` = `codigo_hardware` en BD; tópicos `maquinas/<ID>/...`; `subscribe` a `config` y `ota`; OTA HTTPS (`WiFiClientSecure` + `Update` + SHA256); ejecución OTA en **bucle de espera** (pinza en reposo) y al **fin de `loop()`** (no en `moverPinza`). MQTT TLS opcional: `MQTT_USAR_TLS`.
- [ ] **Partición OTA** en Arduino IDE: usar esquema con dos particiones de app (en CI: `PartitionScheme=min_spiffs`).
- [x] **`.github/workflows/build-firmware.yml`**: compila con `arduino-cli`; sube artifact `firmware-main.bin` + `.sha256`. Publicación al release `firmware-builds` opcional (comentada en el YAML).
- [x] **`ota/manifest.json`**: plantilla con URL de release; reemplazar `sha256` tras cada build publicado.
- [x] **README / HANDOFF**: enlaces y resumen MQTT/OTA (mantener al día con el panel).

## Variables de entorno útiles (servidor del panel)

`MQTT_BROKER`, `MQTT_PORT`, `MQTT_TOPIC_PREFIX`, `MQTT_CONFIG_SUBTOPIC` (default `config`), `MQTT_OTA_SUBTOPIC` (default `ota`), `GITHUB_REPO` (`Rothezee/ReporteGrua`), `OTA_MANIFEST_REF`, `OTA_MANIFEST_URL` (override), `OTA_SHARED_SECRET`, `OTA_ALLOWED_BRANCHES`, `OTA_MANIFEST_CACHE_TTL`.

## Objetivo de ramas distintas

- **main** (o similar): firmware grúa completo.
- **mqtt-only** (u otra): solo reportes MQTT, sin lógica de grúa — mismo repo, distinto binario en el manifiesto; el operador elige rama en el modal OTA del dashboard.

---

*Generado para alinear el firmware con el panel sin duplicar código en AdministrationPanel.*

---

## Estado verificado en este clon (referencia)

Última revisión orientativa del árbol del repo frente al contrato anterior:

| Ítem | Estado |
|------|--------|
| Sketch | [`GruaMQTT/GruaMQTT.ino`](GruaMQTT/GruaMQTT.ino) (convención Arduino / `arduino-cli`). |
| Prefijo `maquinas/` y rutas | `construirTopicsMqtt()`: datos, heartbeat, config, ota desde `ID_DISP`. |
| JSON datos / heartbeat | Formato numérico `action` / `tipo_maquina` / `payload` como en el firmware Gigga (validar con `mqtt_listener.php`). |
| Config + OTA MQTT | `aplicarConfigRemota` + `encolarOtaDesdeJson` en `onMqttMessage`. |
| OTA | HTTPS + SHA256 + `Update`; `ejecutarOtaSiPendiente()` en espera y fin de `loop()`. |
| CI + manifiesto | [`.github/workflows/build-firmware.yml`](.github/workflows/build-firmware.yml), [`ota/manifest.json`](ota/manifest.json). |

Actualizá esta tabla cuando implementes cambios.
