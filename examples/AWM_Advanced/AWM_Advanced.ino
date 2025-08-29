/**
 * AyresWiFiManager - Advanced Example
 * -----------------------------------
 * Showcases:
 *  - Captive portal fallback (AP mode) when WiFi is not configured / fails
 *  - Status LED (connected / portal / disconnected)
 *  - Optional RESET button with short/long press actions
 *  - NTP time sync when connected
 *  - Periodic status prints + RSSI
 *
 * Works on ESP32 and ESP8266.
 *
 * Requirements:
 *  - Place AyresWiFiManager library in your Arduino libraries folder
 *  - Library provides AWM_Logging.h (lightweight logging macros)
 */

#include <Arduino.h>
#include <time.h>
#include <AyresWiFiManager.h>
#include "AWM_Logging.h"   // defines AWM_LOGE/W/I/D/V

#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#else
  #error "This example targets ESP32/ESP8266."
#endif

/* ===================== User Configuration ===================== */

// Captive-portal AP SSID prefix (final SSID will be "AWM-Setup-XXXXXX")
#define AWM_AP_PREFIX        "AWM-Setup"

// Status LED pin (on many boards: ESP32=2, ESP8266=2 (D4))
#ifndef LED_PIN
  #if defined(ESP32)
    #define LED_PIN 2
  #else
    #define LED_PIN 2
  #endif
#endif

// Optional reset/config button pin (GND to press). Use -1 to disable.
#ifndef BTN_PIN
  #define BTN_PIN 0    // BOOT button on many ESP32 devkits. Set -1 to disable.
#endif

// Button press thresholds (ms)
static const uint32_t SHORT_PRESS_MS = 700;    // short: open portal
static const uint32_t LONG_PRESS_MS  = 3000;   // long: wipe credentials

// NTP servers & timezone (adjust to your region)
static const char* NTP1 = "pool.ntp.org";
static const char* NTP2 = "time.google.com";
static const long  GMT_OFFSET_SEC = 0;         // e.g. -10800 for Argentina (-3h)
static const int   DAYLIGHT_OFFSET_SEC = 0;

/* ===================== Globals ===================== */

AyresWiFiManager awm;
bool portalActive = false;
bool wifiReady    = false;

enum LedMode { LED_OFF, LED_ON, LED_BLINK_SLOW, LED_BLINK_FAST };
LedMode ledMode = LED_BLINK_SLOW;
uint32_t ledTicker = 0;
bool ledState = false;

// Button state
#if (BTN_PIN >= 0)
  bool btnPrev = true;          // using INPUT_PULLUP
  uint32_t btnPressedAt = 0;
  bool btnHeld = false;
#endif

/* ===================== Helpers ===================== */

void setLedMode(LedMode m) {
  ledMode = m;
  // set base state instantly for ON/OFF
  if (m == LED_ON)  { digitalWrite(LED_PIN, HIGH); ledState = true; }
  if (m == LED_OFF) { digitalWrite(LED_PIN, LOW);  ledState = false; }
}

void updateLed() {
  const uint32_t now = millis();

  switch (ledMode) {
    case LED_ON:
    case LED_OFF:
      // nothing
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

void printNow() {
  time_t t = time(nullptr);
  struct tm* tm_info = localtime(&t);
  char buf[32];
  if (tm_info && strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info)) {
    AWM_LOGI("Time: %s", buf);
  } else {
    AWM_LOGW("Time not set yet.");
  }
}

void startPortalIfNeeded(const char* reason) {
  AWM_LOGI("Starting captive portal (%s)...", reason);
  awm.startPortal(AWM_AP_PREFIX, ""); // open AP; set a password if you want
  portalActive = true;
  setLedMode(LED_BLINK_FAST);
}

void connectWithFallback() {
  AWM_LOGI("Connecting using saved credentials...");
  if (awm.autoConnect()) {
    wifiReady = true;
    portalActive = false;
    setLedMode(LED_ON);
    AWM_LOGI("Connected! IP: %s", WiFi.localIP().toString().c_str());

    // NTP sync
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP1, NTP2);

    // Give NTP some time
    delay(1000);
    printNow();
  } else {
    wifiReady = false;
    startPortalIfNeeded("autoConnect failed");
  }
}

/* ===================== Setup / Loop ===================== */

void setup() {
  Serial.begin(115200);
  delay(300);

  AWM_LOGI("Using AyresWiFiManager v%s", AWM_VERSION);
  AWM_LOGI("AWM Advanced Example");

  pinMode(LED_PIN, OUTPUT);
  setLedMode(LED_BLINK_SLOW);

#if (BTN_PIN >= 0)
  pinMode(BTN_PIN, INPUT_PULLUP);
#endif

  // Init AyresWiFiManager (AP prefix, AP password="")
  awm.begin(AWM_AP_PREFIX, "");

  connectWithFallback();
}

void loop() {
  // Serve captive portal if active
  if (portalActive) {
    awm.handleClient();

    // If the user has just saved credentials via portal, try to connect
    if (awm.shouldTryConnect()) {
      AWM_LOGI("Credentials updated from portal. Trying to connect...");
      portalActive = false;
      setLedMode(LED_BLINK_SLOW);
      connectWithFallback();
    }
  }

#if (BTN_PIN >= 0)
  // --- Button handling (short/long press)
  bool btnNow = digitalRead(BTN_PIN); // HIGH=not pressed, LOW=pressed
  if (btnPrev && !btnNow) {
    // pressed
    btnPressedAt = millis();
    btnHeld = false;
  } else if (!btnPrev && btnNow) {
    // released
    uint32_t dur = millis() - btnPressedAt;
    if (dur >= LONG_PRESS_MS) {
      AWM_LOGW("Long press: wipe credentials + restart portal.");
      awm.wipeCredentials();          // your library API to clear stored WiFi
      WiFi.disconnect(true);
      delay(100);
      startPortalIfNeeded("long press wipe");
    } else if (dur >= SHORT_PRESS_MS) {
      AWM_LOGI("Short press: open portal.");
      startPortalIfNeeded("short press");
    }
  }
  btnPrev = btnNow;
#endif

  // LED heartbeat
  updateLed();

  // Periodic status
  static uint32_t lastStatus = 0;
  if (millis() - lastStatus > 5000) {
    lastStatus = millis();
    if (WiFi.isConnected()) {
      AWM_LOGI("WiFi OK | IP=%s | RSSI=%d dBm",
               WiFi.localIP().toString().c_str(), WiFi.RSSI());
      // show time if NTP ready
      printNow();
    } else {
      AWM_LOGW("WiFi not connected.");
    }
  }
}
