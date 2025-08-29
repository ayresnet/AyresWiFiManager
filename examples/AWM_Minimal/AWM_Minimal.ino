/**
 * AyresWiFiManager - Minimal Example
 * ----------------------------------
 * Demonstrates how to use AyresWiFiManager (AWM) to connect ESP32/ESP8266
 * to WiFi with captive portal fallback and credentials stored in LittleFS.
 *
 * Features:
 * - If saved credentials are valid → connects automatically.
 * - If no credentials / invalid → starts AP + captive portal for setup.
 *
 * Author: Daniel Salgado (AyresNet)
 * License: MIT
 */

#include <Arduino.h>
#include <AyresWiFiManager.h>

// Optional: set your AP SSID prefix for captive portal mode
#define AWM_AP_PREFIX "AWM-Setup"

AyresWiFiManager awm;

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println(F("\n[AWM] Minimal Example"));

  // Initialize AyresWiFiManager
  // Parameters:
  //  - SSID prefix for AP mode
  //  - Password for AP (optional, "" = open)
  awm.begin(AWM_AP_PREFIX, "");

  Serial.printf("[AWM] Using AyresWiFiManager v%s\n", AWM_VERSION);

  // Try connecting to WiFi (saved credentials in LittleFS)
  if (awm.autoConnect()) {
    Serial.print(F("[AWM] Connected! IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("[AWM] Failed to connect. Captive portal is active."));
  }
}

void loop() {
  // Handle captive portal requests if AP is active
  awm.handleClient();

  // Print WiFi status periodically
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 5000) {
    lastPrint = millis();
    if (WiFi.isConnected()) {
      Serial.print(F("[AWM] WiFi connected. IP: "));
      Serial.println(WiFi.localIP());
    } else {
      Serial.println(F("[AWM] WiFi not connected."));
    }
  }
}
