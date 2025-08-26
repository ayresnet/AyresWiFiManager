<div align="center">

# AyresWiFiManager (AWM)

<!-- Row 1: identity & project status -->
![AWM](https://img.shields.io/badge/AWM-Captive%20Portal-4361ee?style=flat-square)
![Release](https://img.shields.io/github/v/release/ayresnet/AyresWiFiManager?include_prereleases&label=release&style=flat-square)
![License](https://img.shields.io/github/license/ayresnet/AyresWiFiManager?style=flat-square)
![Issues](https://img.shields.io/github/issues/ayresnet/AyresWiFiManager?style=flat-square)

<!-- Row 2: platforms & frameworks -->
![ESP32](https://img.shields.io/badge/ESP32-supported-2ec27e?logo=espressif&logoColor=white&style=flat-square)
![ESP8266](https://img.shields.io/badge/ESP8266-supported-2ec27e?logo=espressif&logoColor=white&style=flat-square)
![Arduino](https://img.shields.io/badge/Arduino-Library-00979D?logo=arduino&logoColor=white&style=flat-square)
![PlatformIO](https://img.shields.io/badge/PlatformIO-ready-f5822a?logo=platformio&logoColor=white&style=flat-square)

<!-- Row 3: key features -->
![LittleFS](https://img.shields.io/badge/LittleFS-OK-2ec27e?style=flat-square)
![Captive Portal](https://img.shields.io/badge/Captive%20Portal-ON-4361ee?style=flat-square)
![DNS catch-all](https://img.shields.io/badge/DNS-catch--all-4cc9f0?style=flat-square)
![NTP](https://img.shields.io/badge/NTP-sync-4cc9f0?style=flat-square)
![ArduinoJson](https://img.shields.io/badge/ArduinoJson-%5E6.21.2-6c757d?logo=json&logoColor=white&style=flat-square)

<!-- (Optional) metrics -->
<!-- ![Downloads](https://img.shields.io/github/downloads/ayresnet/AyresWiFiManager/total?style=flat-square) -->

</div>

<div align="center" style="background-color: #f0f0f0; border: 2px solid #336699; padding: 15px; border-radius: 10px;">
  <table>
    <tr>
      <td style="padding: 5px;">
        <img src="https://res.cloudinary.com/dxunooptp/image/upload/v1756089447/awm01_vyzkbw.png" alt="Screenshot 1" width="250">
      </td>
      <td style="padding: 5px;">
        <img src="https://res.cloudinary.com/dxunooptp/image/upload/v1756089447/awm02_mvzmxr.png" alt="Screenshot 2" width="250">
      </td>
      <td style="padding: 5px;">
        <img src="https://res.cloudinary.com/dxunooptp/image/upload/v1756093584/imagen_2025-08-25_004621649_gpiecd.png" width="250" alt="Screenshot 3">
      </td>
    </tr>
  </table>
</div>

---

📢 Now available in the Arduino IDE!

You can install AyresWiFiManager directly from the Arduino IDE Library Manager.  
Just search for “AyresWiFiManager” and click “Install”.

---
**AWM — Pro Wi‑Fi manager for ESP32/ESP8266** featuring a real captive portal (AP+DNS), modern UI served from **LittleFS**, secure credential storage, **fallback policies**, provisioning button, status LED, **NTP**, Internet reachability check, and configurable **logging**.

> Project: https://github.com/AyresNet/AyresWiFiManager  
> License: MIT — (c) 2025 AyresNet

> 🇪🇸 ¿Español? Lee el [README.es.md](README.es.md).

---

## ⭐ Why AyresWiFiManager?

- **Real captive portal**: DNS *catch‑all* + OS connectivity routes (Android/iOS/Windows) to force open `http://192.168.4.1`.
- **Lightweight UI (no external deps)** served from `/data` (LittleFS): search, rescan, signal bars, and an option to erase credentials from the portal.
- **Clear, cross‑platform API** for ESP32/ESP8266 with explicit policies.
- **Smart fallbacks**: `NO_CREDENTIALS_ONLY` (default), `ON_FAIL`, `SMART_RETRIES`, `BUTTON_ONLY`, `NEVER`.
- **Integrated Button & LED**:
  - Button (LOW): **2–5 s** opens portal · **≥5 s** erases JSON and restarts.
  - LED: `ON` connected · `BLINK_SLOW` portal · `BLINK_FAST` scanning · `OFF` idle (+ double/triple patterns for feedback).
- **Safe JSON erase** with whitelist, recursive on ESP32.
- **Professional logging** (`AWM_LOG*` macros) with level set via `build_flags`.
- **Utilities**: NTP (pool.ntp.org/time.nist.gov), `hayInternet()` (Google `204`), RSSI, timestamp, auto‑reconnect.

---

## 🚀 Features

- **Credential storage** at `/wifi.json` (LittleFS).
- **Portal routes**
  - `GET /` → `index.html`
  - `POST /save` → stores `{ssid,password}` and restarts
  - `GET /scan` or `/scan.json` → `[{ssid,rssi,secure}]`
  - `POST /erase` → deletes `.json` (respects whitelist) and restarts
- **Example HTML/JS/CSS** included: `data/index.html`, `data/success.html`, `data/error.html`
- **ESP32**: `esp_wifi_set_ps(WIFI_PS_NONE)`, `LittleFS.begin(true)` (auto‑format)  
  **ESP8266**: `WiFi.setSleepMode(WIFI_NONE_SLEEP)`, non‑recursive LittleFS iteration

---

## 📦 Installation

### PlatformIO (recommended)

Minimal `platformio.ini`:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.filesystem = littlefs

; Optional: logging
build_flags =
  -D AWM_ENABLE_LOG=1     ; 0=off, 1=on
  -D AWM_LOG_LEVEL=3      ; 1=E, 2=W, 3=I, 4=D, 5=V
```

- Place your portal files in `data/` (e.g. `index.html`).
- Upload FS: `pio run -t uploadfs`.
- Flash the sketch and open the serial monitor.

### Arduino IDE

- Install the official ESP32/ESP8266 cores.
- Install the appropriate **LittleFS uploader** (ESP32/ESP8266).
- Upload the `data/` folder to the FS with the “Data Upload” tool.
- Build and upload the sketch.

---

## ✏️ Minimal usage

```cpp
#include <AyresWiFiManager.h>

AyresWiFiManager wifi;

void setup() {
  Serial.begin(115200);

  wifi.setAPCredentials("ayreswifimanager","123456789");
  wifi.setPortalTimeout(300);   // 5 min of inactivity
  wifi.setAPClientCheck(true);  // don't close if clients connected
  wifi.setWebClientCheck(true); // each HTTP request resets the timer
  // wifi.setCaptivePortal(false); // disable redirections if you prefer

  wifi.begin();  // mounts FS, loads /wifi.json if present
  wifi.run();    // tries STA; if it fails applies the fallback policy
}

void loop() {
  wifi.update(); // serves HTTP/DNS and handles timeouts/LED
}
```

---

## 🪄 Recommended boot flow

The example **`examples/30sVentana/src/main.cpp`** does:

- If **credentials exist** → opens portal with **30 s** *inactivity timeout*; if unused, it closes and continues automatically.
- If **no credentials** → opens a “normal” portal (e.g. **5 min** inactivity timeout).
- In both cases, while the portal is in use (AP clients or HTTP requests), it **won’t close** until usage stops.

---

## 🧩 API overview

```cpp
// Portal / AP
void setHtmlPathPrefix(const String& prefix);  // e.g.: "/wifimanager"
void setHostname(const String& host);
void setAPCredentials(const String& ssid, const String& pass);
void setCaptivePortal(bool enabled);           // DNS + captive routes
void setPortalTimeout(uint32_t seconds);       // 0 = no timeout
void setAPClientCheck(bool enabled);           // keep open if clients connected
void setWebClientCheck(bool enabled);          // each request resets timer
void openPortal();
void closePortal();
bool isPortalActive() const;

// Fallback
enum class FallbackPolicy { ON_FAIL, NO_CREDENTIALS_ONLY, SMART_RETRIES, BUTTON_ONLY, NEVER };
void setFallbackPolicy(FallbackPolicy p);
void setSmartRetries(uint8_t maxRetries, uint32_t windowMs);
void enableButtonPortal(bool enable);
void setAutoReconnect(bool enabled);

// Status / utilities
bool tieneCredenciales() const;
bool connectToWiFi();
bool isConnected();
int  getSignalStrength();     // RSSI
uint64_t getTimestamp();      // ms (0 if no NTP)
bool hayInternet();           // generate_204
bool scanRedDetectada();
void forzarReconexion();

// LED
enum class LedPattern { OFF, ON, BLINK_SLOW, BLINK_FAST, BLINK_DOUBLE, BLINK_TRIPLE };
void setLedAuto(bool enable);
void setLedPatternManual(LedPattern p);

// FS safety (.json whitelist)
void setProtectedJsons(std::initializer_list<const char*> names); // e.g.: {"/wifi.json","license.json"}
```

---

## 🔘 Button & 💡 LED

- **Button** (default pin **0**, `INPUT_PULLUP`, active LOW):
  - **2–5 s** → open portal (if `enableButtonPortal(true)`)
  - **≥5 s** → erase `.json` (respects `setProtectedJsons`) and restart
- **LED** (default pin **2**):
  - `ON` connected to Wi‑Fi
  - `BLINK_SLOW` portal active
  - `BLINK_FAST` scanning
  - `BLINK_DOUBLE / TRIPLE` feedback while holding the button
  - `OFF` no link/portal

> You can set the pattern manually (`setLedPatternManual`) or leave it automatic (`setLedAuto(true)`).

---

## 📋 Logging (optional, recommended)

Include `include/AWM_Logging.h` and use the macros:

```cpp
AWM_LOGE("error: %d", code);   // Error
AWM_LOGW("warning...");
AWM_LOGI("info...");
AWM_LOGD("debug x=%d", x);
AWM_LOGV("verbose...");
```

Enable via **compile flags** (PlatformIO):

```ini
build_flags =
  -D AWM_ENABLE_LOG=1
  -D AWM_LOG_LEVEL=3    ; 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG, 5=VERBOSE
```

- Set `AWM_ENABLE_LOG=0` to turn **all** logs off (no‑ops).
- Raise/lower `AWM_LOG_LEVEL` to control verbosity.

---

## 🗂 Portal files (LittleFS)

- `data/index.html` – scan, filter, SSID selection, save form, advanced options  
- `data/success.html` – saved confirmation (SVG with pulse; the device restarts)  
- `data/error.html` – save error (SVG with subtle shake + “Back” link)

> Change the root with `setHtmlPathPrefix("/wifimanager")` if you prefer another folder.

**Upload FS**  
- PlatformIO: `pio run -t uploadfs`  
- Arduino IDE: “Data Upload” tool

---

## 🔁 Migrating from tzapu/WiFiManager

AyresWiFiManager aims to be **simple and explicit**:

- No configuration *auto‑magic*: you decide **when** to open the portal and **for how long**.
- A truly **captive** portal (DNS + routes), not just AP + webserver.
- Modern UI without CDNs, tailored for low RAM/flash ESPs.
- **Safe erase** of `.json` with whitelist and (on ESP32) **recursive**.
- **Logging** with adjustable levels.

---

## 📚 Examples

- `examples/standard/src/main.cpp` – simple standard flow  
- `examples/30sVentana/src/main.cpp` – 30 s “boot window” if credentials exist

---

## 🗃 Repository structure

```
data/                  # Portal HTML/CSS/JS (LittleFS)
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

> To keep local libs out of the repo, add paths to `.gitignore` (e.g. `lib/AyresShell/`).

---

## ✅ Production tips

- **Protect** critical files:  
  `wifi.setProtectedJsons({"/wifi.json","license.json"});`
- Set coherent **inactivity timeouts** (`setPortalTimeout`) and enable `setAPClientCheck(true)` + `setWebClientCheck(true)` so it doesn’t close while in use.
- For remote installs, `setFallbackPolicy(SMART_RETRIES)` + `setAutoReconnect(true)` usually gives a great UX.
- If you need fewer logs in production: `-D AWM_ENABLE_LOG=0`.

---

## 🧩 Compatibility

- **ESP32** (Arduino core)  
- **ESP8266** (Arduino core)

> Requires **LittleFS**. On ESP32 it auto‑formats if `begin(true)` fails.

---

## 🤝 Contributing

PRs & issues are welcome!  
Ideas: more examples, translations, portal UI/UX tweaks, new fallback policies.

---

## 📄 License

MIT — see `LICENSE` (or SPDX headers in source files).

---

> 📘 [Read this in Spanish → README.es.md](README.es.md)

Questions or ideas? Open an issue. Thanks for trying **AyresWiFiManager**! 🚀

