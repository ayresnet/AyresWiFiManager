/**
 * AyresWiFiManager - Advanced_OK (Arduino IDE friendly)
 * =====================================================
 * 
 * Description:
 * ------------
 * Advanced usage example of AyresWiFiManager v2.0.1 for ESP32/ESP8266.
 * 
 * Key Features:
 *  - Automatic connection using stored credentials (STA mode).
 *  - Fallback to captive portal (AP + DNS) when WiFi is not available.
 *  - Status LED indicator (ON, OFF, BLINK_SLOW, BLINK_FAST).
 *  - Optional physical button:
 *      • Short press (≥700 ms): open captive portal.
 *      • Long press (planned): wipe credentials.
 *  - No use of private or obsolete APIs.
 *  - Includes NTP time synchronization when connected.
 * 
 * Requirements:
 * -------------
 * 1. Install AyresWiFiManager v2.0.1 (or higher).
 * 2. Prepare HTML files in LittleFS (upload with "ESP32 LittleFS Data Upload"
 *    or "pio run --target uploadfs" in PlatformIO). Place them in /data:
 *       - index.html   → main portal page
 *       - success.html → shown after saving credentials
 *       - error.html   → shown if something fails
 * 
 * Pinout (can be changed):
 *  - LED_PIN: GPIO 2 (default).
 *  - BTN_PIN: GPIO 0 (BOOT on ESP32). Use -1 to disable.
 * 
 * Usage:
 * ------
 *  - On startup, attempts to connect using saved credentials.
 *  - If it fails within 8 seconds, it opens the captive portal for reconfiguration.
 *  - If BOOT is held down during startup → directly open captive portal.
 * 
 * Compatibility:
 * --------------
 *  - ESP32 (Arduino core)
 *  - ESP8266 (Arduino core)
 *
 * Author:
 * -------
 *  Daniel C. Salgado – AyresNet
 * 
 * License:
 * --------
 *  MIT
 */

#include <Arduino.h>
#include <AyresWiFiManager.h>
// #include "AWM_Logging.h"   // Si no lo tenés, dejalo comentado y usá Serial.println

#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#else
  #error "Este ejemplo requiere ESP32 o ESP8266"
#endif

/* ===================== Config del usuario ===================== */

// LED de estado (en muchos devkits: GPIO 2)
#ifndef LED_PIN
  #define LED_PIN 2
#endif

// Botón opcional (a GND). Usá -1 para desactivar.
#ifndef BTN_PIN
  #define BTN_PIN 0    // BOOT en muchos ESP32. Cambiá a -1 si no usás botón.
#endif

// Umbrales de pulsación (ms)
static const uint32_t SHORT_PRESS_MS = 700;    // corto: abrir portal
// static const uint32_t LONG_PRESS_MS  = 3000; // si luego añadís "wipe", lo podés usar

// NTP
static const char* NTP1 = "pool.ntp.org";
static const char* NTP2 = "time.google.com";
static const long  GMT_OFFSET_SEC = 0;         // ajustá a tu zona
static const int   DAYLIGHT_OFFSET_SEC = 0;

/* ===================== Estado global ===================== */

AyresWiFiManager awm;
bool portalActive = false;

enum LedMode { LED_OFF, LED_ON, LED_BLINK_SLOW, LED_BLINK_FAST };
LedMode ledMode = LED_BLINK_SLOW;
uint32_t ledTicker = 0;
bool ledState = false;

#if (BTN_PIN >= 0)
  bool btnPrev = true;          // con INPUT_PULLUP
  uint32_t btnPressedAt = 0;
#endif

/* ===================== Utilidades ===================== */

void setLedMode(LedMode m) {
  ledMode = m;
  if (m == LED_ON)  { digitalWrite(LED_PIN, HIGH); ledState = true; }
  if (m == LED_OFF) { digitalWrite(LED_PIN, LOW);  ledState = false; }
}

void updateLed() {
  const uint32_t now = millis();
  switch (ledMode) {
    case LED_ON:
    case LED_OFF:
      break;
    case LED_BLINK_SLOW:
      if (now - ledTicker >= 800) { // ~1.25 Hz
        ledTicker = now;
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
      }
      break;
    case LED_BLINK_FAST:
      if (now - ledTicker >= 200) { // 5 Hz
        ledTicker = now;
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
      }
      break;
  }
}

static void logI(const String& s){ Serial.println(s); }
static void logW(const String& s){ Serial.println(s); }

/* Opcional: mostrar hora si NTP ya sincronizó */
void printTimeIfReady() {
  time_t t = time(nullptr);
  struct tm* tm_info = localtime(&t);
  char buf[32];
  if (tm_info && strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info)) {
    Serial.print("Time: "); Serial.println(buf);
  } else {
    Serial.println("Time not set yet.");
  }
}

// Abrir portal cautivo usando API pública
void startPortalNow(const char* reason) {
  Serial.print("Starting captive portal ("); Serial.print(reason); Serial.println(")...");
  awm.openPortal();        // API pública v2.0.1
  portalActive = true;
  setLedMode(LED_BLINK_FAST);
}

/**
 * Intenta conectar usando credenciales guardadas (lo hace awm.begin()).
 * Si no conecta en el timeout, abre portal.
 */
void connectOrPortal(uint32_t timeoutMs = 8000) {
  Serial.println("AWM begin() + intento de conexión...");
  awm.begin();                    // API actual: sin argumentos
  setLedMode(LED_BLINK_SLOW);

  uint32_t t0 = millis();
  while (!WiFi.isConnected() && millis() - t0 < timeoutMs) {
    updateLed();
    delay(100);
    yield(); // no bloquear watchdog
  }

  if (awm.isConnected()) {
    setLedMode(LED_ON);
    Serial.print("WiFi conectado. IP: ");
    Serial.println(WiFi.localIP());
    // NTP
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP1, NTP2);
    delay(800);
    printTimeIfReady();
    portalActive = false;
  } else {
    Serial.println("No se logró conexión WiFi. Abriendo portal...");
    startPortalNow("connect timeout");
  }
}

/* ===================== Setup / Loop ===================== */

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(LED_PIN, OUTPUT);
  setLedMode(LED_BLINK_SLOW);

#if (BTN_PIN >= 0)
  pinMode(BTN_PIN, INPUT_PULLUP);
#endif

#ifdef AWM_VERSION
  Serial.print("AyresWiFiManager v"); Serial.println(AWM_VERSION);
#else
  Serial.println("AyresWiFiManager (version macro not defined)");
#endif

#if (BTN_PIN >= 0)
  // Si mantenés BOOT apretado al encender, abrí portal directamente
  if (digitalRead(BTN_PIN) == LOW) {
    Serial.println("BTN hold at boot → Portal directo");
    startPortalNow("boot hold");
    return;
  }
#endif

  // Primer intento: conectar con credenciales guardadas; si falla → portal
  connectOrPortal(8000);
}

void loop() {
  awm.update(); // IMPORTANTE en v2.0.1
  updateLed();

#if (BTN_PIN >= 0)
  // Pulsación corta → abrir portal (útil si perdiste la red o querés reconfigurar)
  bool btnNow = digitalRead(BTN_PIN); // HIGH=libre, LOW=presionado
  if (btnPrev && !btnNow) {
    btnPressedAt = millis(); // presionó
  } else if (!btnPrev && btnNow) {
    uint32_t dur = millis() - btnPressedAt;
    if (dur >= SHORT_PRESS_MS) {
      Serial.println("Short press → abrir portal");
      startPortalNow("short press");
    }
  }
  btnPrev = btnNow;
#endif

  // Reintento periódico si no hay WiFi ni portal activo
  static uint32_t lastRetry = 0;
  if (!portalActive && !awm.isConnected() && millis() - lastRetry > 15000) {
    lastRetry = millis();
    Serial.println("WiFi no conectado → reintento");
    connectOrPortal(5000);
  }

  // Log de estado cada 5s
  static uint32_t lastStatus = 0;
  if (millis() - lastStatus > 5000) {
    lastStatus = millis();
    if (awm.isConnected()) {
      Serial.print("WiFi OK | IP="); Serial.print(WiFi.localIP());
      Serial.print(" | RSSI="); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
      printTimeIfReady();
    } else {
      Serial.println("WiFi not connected.");
    }
  }
}
