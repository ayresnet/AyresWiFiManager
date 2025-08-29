/**
 * AyresWiFiManager - Minimal_OK (Arduino IDE friendly)
 * ====================================================
 *
 * Description:
 * ------------
 * Minimal example of using AyresWiFiManager v2.0.1 with ESP32/ESP8266.
 *
 * Key Features:
 *  - If saved credentials are valid → connects automatically (STA mode).
 *  - If no credentials or WiFi fails → automatically falls back to captive portal (AP + DNS).
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
 * Usage:
 * ------
 *  - On boot, the device tries to connect with saved credentials.
 *  - If it cannot connect, a captive portal opens automatically.
 *  - Once credentials are saved, device will reconnect automatically on next boot.
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

AyresWiFiManager awm;

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.printf("\n[AWM] Minimal Example - v%s\n", AWM_VERSION);

  // Initialize and attempt to connect using saved credentials
  awm.begin();

  // If WiFi connects, print IP
  if (awm.isConnected()) {
    Serial.print("[AWM] Connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[AWM] No WiFi connection. Captive portal active.");
  }
}

void loop() {
  // Mandatory to keep AWM running (portal, reconnection, etc.)
  awm.update();

  // Periodic status print
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 5000) {
    lastPrint = millis();
    if (awm.isConnected()) {
      Serial.print("[AWM] WiFi connected. IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("[AWM] WiFi not connected.");
    }
  }
}
