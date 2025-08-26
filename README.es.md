<div align="center">

# AyresWiFiManager (AWM)

<!-- Línea 1: identidad y estado del proyecto -->
![AWM](https://img.shields.io/badge/AWM-Portal%20Cautivo-4361ee?style=flat-square)
![Release](https://img.shields.io/github/v/release/ayresnet/AyresWiFiManager?include_prereleases&label=release&style=flat-square)
![License](https://img.shields.io/github/license/ayresnet/AyresWiFiManager?style=flat-square)
![Issues](https://img.shields.io/github/issues/ayresnet/AyresWiFiManager?style=flat-square)

<!-- Línea 2: plataformas y frameworks -->
![ESP32](https://img.shields.io/badge/ESP32-supported-2ec27e?logo=espressif&logoColor=white&style=flat-square)
![ESP8266](https://img.shields.io/badge/ESP8266-supported-2ec27e?logo=espressif&logoColor=white&style=flat-square)
![Arduino](https://img.shields.io/badge/Arduino-Library-00979D?logo=arduino&logoColor=white&style=flat-square)
![PlatformIO](https://img.shields.io/badge/PlatformIO-ready-f5822a?logo=platformio&logoColor=white&style=flat-square)

<!-- Línea 3: características clave -->
![LittleFS](https://img.shields.io/badge/LittleFS-OK-2ec27e?style=flat-square)
![Captive Portal](https://img.shields.io/badge/Captive%20Portal-ON-4361ee?style=flat-square)
![DNS catch-all](https://img.shields.io/badge/DNS-catch--all-4cc9f0?style=flat-square)
![NTP](https://img.shields.io/badge/NTP-sync-4cc9f0?style=flat-square)
![ArduinoJson](https://img.shields.io/badge/ArduinoJson-%5E6.21.2-6c757d?logo=json&logoColor=white&style=flat-square)

<!-- (Opcional) métricas -->
<!-- ![Downloads](https://img.shields.io/github/downloads/ayresnet/AyresWiFiManager/total?style=flat-square) -->

</div>

<div align="center" style="background-color: #f0f0f0; border: 2px solid #336699; padding: 15px; border-radius: 10px;">
  <table>
    <tr>
      <td style="padding: 5px;">
        <img src="https://res.cloudinary.com/dxunooptp/image/upload/v1756089447/awm01_vyzkbw.png" alt="Imagen 1" width="250">
      </td>
      <td style="padding: 5px;">
        <img src="https://res.cloudinary.com/dxunooptp/image/upload/v1756089447/awm02_mvzmxr.png" alt="Imagen 2" width="250">
      </td>
      <td style="padding: 5px;">
        <img src="https://res.cloudinary.com/dxunooptp/image/upload/v1756093584/imagen_2025-08-25_004621649_gpiecd.png" width="250">
      </td>
    </tr>
  </table>
</div>

---

📢 ¡Disponible en el Arduino IDE!

Instalá AyresWiFiManager directamente desde el Library Manager del IDE de Arduino.  
Solo buscá “AyresWiFiManager” y tocá “Instalar”.

---

**AWM - Gestor Wi‑Fi “pro” para ESP32/ESP8266** con portal cautivo real (AP+DNS), UI moderna servida desde **LittleFS**, almacenamiento seguro de credenciales, **políticas de fallback**, botón de provisión, LED de estado, **NTP**, chequeo de Internet y **logging** configurable.

> Proyecto: https://github.com/AyresNet/AyresWiFiManager  
> Licencia: MIT — (c) 2025 AyresNet

---

## ⭐ ¿Por qué AyresWiFiManager?

- **Portal cautivo real**: DNS *catch‑all* + rutas de detección (Android/iOS/Windows) para forzar apertura en `http://192.168.4.1`.
- **UI ligera (sin dependencias externas)** servida desde `/data` (LittleFS): búsqueda, reescaneo, barras de señal y opción de borrar credenciales desde el portal.
- **API clara** y multiplataforma (ESP32/ESP8266) con políticas explícitas.
- **Fallbacks inteligentes**: `NO_CREDENTIALS_ONLY` (default), `ON_FAIL`, `SMART_RETRIES`, `BUTTON_ONLY`, `NEVER`.
- **Botón y LED** integrados:
  - Botón (LOW): **2–5 s** abre portal · **≥5 s** borra JSON y reinicia.
  - LED: `ON` conectado · `BLINK_SLOW` portal · `BLINK_FAST` escaneo · `OFF` idle (+ patrones dobles/triples para feedback).
- **Borrado seguro** de `.json` con **lista blanca** y recursivo en ESP32.
- **Logging profesional** (macros `AWM_LOG*`) con nivel ajustable por `build_flags`.
- **Utilidades**: NTP (pool.ntp.org/time.nist.gov), `hayInternet()` (Google `204`), RSSI, timestamp, reconexión automática.

---

## 🚀 Características

- **Almacenamiento de credenciales** en `/wifi.json` (LittleFS).
- **Rutas del portal**
  - `GET /` → `index.html`
  - `POST /save` → guarda `{ssid,password}` y reinicia
  - `GET /scan` o `/scan.json` → `[{ssid,rssi,secure}]`
  - `POST /erase` → borra `.json` (respeta protegidos) y reinicia
- **HTML/JS/CSS** de ejemplo incluidos: `data/index.html`, `data/success.html`, `data/error.html`
- **ESP32**: `esp_wifi_set_ps(WIFI_PS_NONE)`, `LittleFS.begin(true)` (auto‑formateo)  
  **ESP8266**: `WiFi.setSleepMode(WIFI_NONE_SLEEP)`, iteración LittleFS no recursiva

---

## 📦 Instalación

### PlatformIO (recomendado)

`platformio.ini` mínimo:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.filesystem = littlefs

; Opcional: logging
build_flags =
  -D AWM_ENABLE_LOG=1     ; 0=off, 1=on
  -D AWM_LOG_LEVEL=3      ; 1=E, 2=W, 3=I, 4=D, 5=V
```

- Coloca tus archivos del portal en `data/` (por ejemplo `index.html`).
- Sube el FS: `pio run -t uploadfs`.
- Flashea el sketch y abre el monitor serie.

### Arduino IDE

- Añadí las cores ESP32/ESP8266 oficiales.
- Instalá el **uploader de LittleFS** correspondiente (ESP32/ESP8266).
- Subí la carpeta `data/` al FS con la herramienta “Data Upload”.
- Compilá y cargá el sketch.

---

## ✏️ Uso mínimo

```cpp
#include <AyresWiFiManager.h>

AyresWiFiManager wifi;

void setup() {
  Serial.begin(115200);

  wifi.setAPCredentials("ayreswifimanager","123456789");
  wifi.setPortalTimeout(300);   // 5 min por inactividad
  wifi.setAPClientCheck(true);  // no cierra si hay clientes
  wifi.setWebClientCheck(true); // cada request reinicia el timer
  // wifi.setCaptivePortal(false); // desactiva redirecciones, si querés

  wifi.begin();  // monta FS, carga /wifi.json si existe
  wifi.run();    // intenta STA; si falla aplica la política de fallback
}

void loop() {
  wifi.update(); // sirve HTTP/DNS y maneja timeouts/LED
}
```

---

## 🪄 Flujo de arranque recomendado

El ejemplo **`examples/30sVentana/src/main.cpp`** hace:

- Si **hay** credenciales → abre portal con *timeout por inactividad* de **30 s**; si no lo usás, se cierra solo y continúa.
- Si **no hay** credenciales → abre portal “normal” (ej. **5 min** por inactividad).
- En ambos casos, si usás el portal (clientes AP o requests HTTP), **no se cierra** hasta que dejes de usarlo.

---

## 🧩 API principal

```cpp
// Portal / AP
void setHtmlPathPrefix(const String& prefix);  // ej: "/wifimanager"
void setHostname(const String& host);
void setAPCredentials(const String& ssid, const String& pass);
void setCaptivePortal(bool enabled);           // DNS + rutas captive
void setPortalTimeout(uint32_t seconds);       // 0 = sin timeout
void setAPClientCheck(bool enabled);           // no cerrar si hay clientes
void setWebClientCheck(bool enabled);          // cada request reinicia timer
void openPortal();
void closePortal();
bool isPortalActive() const;

// Fallback
enum class FallbackPolicy { ON_FAIL, NO_CREDENTIALS_ONLY, SMART_RETRIES, BUTTON_ONLY, NEVER };
void setFallbackPolicy(FallbackPolicy p);
void setSmartRetries(uint8_t maxRetries, uint32_t windowMs);
void enableButtonPortal(bool enable);
void setAutoReconnect(bool enabled);

// Estado / utilidades
bool tieneCredenciales() const;
bool connectToWiFi();
bool isConnected();
int  getSignalStrength();     // RSSI
uint64_t getTimestamp();      // ms (0 si no hay NTP)
bool hayInternet();           // generate_204
bool scanRedDetectada();
void forzarReconexion();

// LED
enum class LedPattern { OFF, ON, BLINK_SLOW, BLINK_FAST, BLINK_DOUBLE, BLINK_TRIPLE };
void setLedAuto(bool enable);
void setLedPatternManual(LedPattern p);

// Seguridad FS (.json protegidos)
void setProtectedJsons(std::initializer_list<const char*> names); // ej: {"/wifi.json","licencia.json"}
```

---

## 🔘 Botón y 💡 LED

- **Botón** (pin por defecto **0**, `INPUT_PULLUP`, activo en LOW):
  - **2–5 s** → abre portal (si `enableButtonPortal(true)`)
  - **≥5 s** → borra `.json` (respeta `setProtectedJsons`) y reinicia
- **LED** (pin por defecto **2**):
  - `ON` conectado a Wi‑Fi
  - `BLINK_SLOW` portal activo
  - `BLINK_FAST` escaneando
  - `BLINK_DOUBLE / TRIPLE` feedback al mantener botón
  - `OFF` sin enlace/portal

> Podés fijar el patrón manual (`setLedPatternManual`) o dejarlo automático (`setLedAuto(true)`).

---

## 📋 Logging (opcional, recomendado)

Incluye `include/AWM_Logging.h` con macros:

```cpp
AWM_LOGE("error: %d", code);   // Error
AWM_LOGW("warning...");
AWM_LOGI("info...");
AWM_LOGD("debug x=%d", x);
AWM_LOGV("verbose...");
```

Activación por **flags de compilación** (PlatformIO):

```ini
build_flags =
  -D AWM_ENABLE_LOG=1
  -D AWM_LOG_LEVEL=3    ; 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG, 5=VERBOSE
```

- Poné `AWM_ENABLE_LOG=0` para apagar **todos** los logs (macro‑noops).
- Subí/bajá `AWM_LOG_LEVEL` según lo que quieras ver.

---

## 🗂 Archivos del portal (LittleFS)

- `data/index.html` – escaneo, filtro, selección de SSID, formulario de guardado, opciones avanzadas  
- `data/success.html` – confirmación de guardado (SVG con pulso; el equipo se reinicia)  
- `data/error.html` – error de guardado (SVG con vibración sutil + enlace “Volver”)

> Cambiá la raíz con `setHtmlPathPrefix("/wifimanager")` si preferís otra carpeta.

**Subida del FS**  
- PlatformIO: `pio run -t uploadfs`  
- Arduino IDE: herramienta “Data Upload”

---

## 🔁 Migrando desde tzapu/WiFiManager

AyresWiFiManager busca ser **simple y explícito**:

- Sin *auto‑magia* de configuración: vos decidís **cuándo** abrir portal y **por cuánto**.
- Portal verdaderamente **cautivo** (DNS + rutas), no solo un AP + webserver.
- UI moderna sin CDNs, pensada para ESPs con poca RAM/flash.
- **Borrado seguro** de `.json` con whitelist y (en ESP32) **recursivo**.
- **Logging** integrable y niveles ajustables.

---

## 📚 Ejemplos

- `examples/standard/src/main.cpp` – flujo simple estándar  
- `examples/30sVentana/src/main.cpp` – “ventana” de arranque de 30 s si hay credenciales

---

## 🗃 Estructura del repo

```
data/                  # HTML/CSS/JS del portal (LittleFS)
examples/
  standard/src/main.cpp
  30sVentana/src/main.cpp
include/
  AWM_Logging.h
src/
  ayreswifimanager.h
  ayreswifimanager.cpp
platformio.ini
library.properties
library.json
README.md
```

> Si querés mantener librerías locales fuera del repo, añadí rutas a `.gitignore` (ej.: `lib/AyresShell/`).

---

## ✅ Consejos de producción

- **Protegé** archivos críticos:  
  `wifi.setProtectedJsons({"/wifi.json","licencia.json"});`
- Definí **timeouts por inactividad** coherentes (`setPortalTimeout`) y activá `setAPClientCheck(true)` + `setWebClientCheck(true)` si no querés que se cierre mientras lo usan.
- Para instalaciones remotas, `setFallbackPolicy(SMART_RETRIES)` + `setAutoReconnect(true)` suelen dar buena experiencia.
- Si necesitás ahorrar logs en producción: `-D AWM_ENABLE_LOG=0`.

---

## 🧩 Compatibilidad

- **ESP32** (Arduino core)  
- **ESP8266** (Arduino core)

> Requiere **LittleFS**. En ESP32 se auto‑formatea si `begin(true)` falla.

---

## 🤝 Contribuir

¡PRs e issues son bienvenidos!  
Ideas: ejemplos adicionales, traducciones, mejoras UI/UX del portal, nuevas políticas de fallback.

---

## 📄 Licencia

MIT — ver archivo `LICENSE` (o cabeceras SPDX en el código).

---

> 📘 [Leer en ingles → README.md](README.md)

¿Dudas o ideas? Abrí un issue en el repo. ¡Gracias por probar **AyresWiFiManager**! 🚀
