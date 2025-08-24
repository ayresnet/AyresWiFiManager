/*
 *  SPDX-License-Identifier: MIT
 *  Ejemplo — AyresWiFiManager (ESP32/ESP8266)
 *  ---------------------------------------------------------------
 *  @file      main.cpp
 *  @brief     Ejemplo de uso “listo para flashear” del AyresWiFiManager.
 *
 *  Qué muestra este ejemplo
 *  ---------------------------------------------------------------
 *  • Configura SSID/Pass del AP del portal y hostname.
 *  • Activa timeout del portal (5 min) con política de “no cerrar si hay STAs”
 *    y reinicio del timer ante cada request HTTP.
 *  • Intenta conectar con /wifi.json si existe; si no, abre portal según
 *    la política elegida (NO_CREDENTIALS_ONLY por defecto).
 *  • Logea RSSI e “Internet OK/NO” (generate_204) cada 30 s cuando conecta.
 *  • Habilita reconexión automática y NTP al reconectar.
 *
 *  Tips:
 *  • setCaptivePortal(false) → portal sin redirecciones; entrás manual a
 *    http://192.168.4.1.
 *  • setProtectedJsons({ "wifi.json", "licencia.json" }) protege esos JSON
 *    de la limpieza por botón ≥5 s.
 *
 *  Autor       : (Daniel Salgado) — AyresNet
 *  Versión     : 2.0.0
 *  Licencia    : MIT
 */


#include <Arduino.h>
#include "AyresWiFiManager.h"

// ───────── Objetos ─────────────────────────────────────────────
AyresWiFiManager wifiManager;   // LED=2, BTN=0 (por defecto)

// ───────── Estado/log periódico ────────────────────────────────
static uint32_t lastNetCheck = 0;
static const uint32_t NET_CHECK_MS = 30000;  // 30 s

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("==== AyresWiFiManager (pro) ====");

  // === RUTA DE HTML DEL PORTAL ===
  // Por defecto busca en la RAÍZ del FS:  "/index.html"
  // Si usás carpeta: /wifimanager/  (index.html, success.html, error.html)
  // wifiManager.setHtmlPathPrefix("/wifimanager");
  // Si subiste en la raíz, podés dejar el default o forzar así:
  // wifiManager.setHtmlPathPrefix("/");

  // === Hostname + SSID/Pass del AP del portal cautivo ===
  wifiManager.setHostname("ayreswifimanager");
  wifiManager.setAPCredentials("ayreswifimanager", "123456789");

  // === Comportamiento del portal cautivo ===
  //wifiManager.setCaptivePortal(false);   // redirect DNS a 192.168.4.1
  wifiManager.setPortalTimeout(300);    // cierra portal si 5 min sin uso
  wifiManager.setAPClientCheck(true);   // el timeout no corre si hay clientes
  wifiManager.setWebClientCheck(true);  // cada request reinicia timeout

  // Protegé exactamente esos archivos .json:
  //wifiManager.setProtectedJsons({"/licencia.json", "/secret.json", "wifi.json"});

  // ========= ELEGÍ UNA POLÍTICA DE FALLBACK =========
  // A) Abrir portal AUTOMÁTICAMENTE solo si NO hay credenciales (provisión)
  // wifiManager.setFallbackPolicy(AyresWiFiManager::FallbackPolicy::NO_CREDENTIALS_ONLY);

  // B) Abrir portal SOLO con botón 2–5s (nunca automático)  ← lo que pediste
  //wifiManager.setFallbackPolicy(AyresWiFiManager::FallbackPolicy::BUTTON_ONLY);
  wifiManager.enableButtonPortal(true);

  // C) Abrir portal si falla la conexión
  // wifiManager.setFallbackPolicy(AyresWiFiManager::FallbackPolicy::ON_FAIL);

  // D) Abrir portal tras N fallos en T ms
  // wifiManager.setFallbackPolicy(AyresWiFiManager::FallbackPolicy::SMART_RETRIES);
  // wifiManager.setSmartRetries(3, 60000);

  // === Inicializa FS/GPIO/WiFi y carga credenciales ===
  wifiManager.begin();

  // Intenta conectar (si conecta hace NTP). Si no, aplicará la política anterior.
  wifiManager.run();

  // Autoreconexión del driver + lógica propia de la lib
  wifiManager.setAutoReconnect(true);

  // Log de hora (si NTP ya se sincronizó dentro de la lib)
  const uint64_t ts = wifiManager.getTimestamp();
  if (ts > 0) {
    Serial.printf("⏱️ Timestamp (ms): %llu\n", (unsigned long long)ts);
  } else {
    Serial.println("⏱️ NTP no sincronizado aún.");
  }

  Serial.println("💡 Botón: 2-5s abre portal, ≥5s borra credenciales.");
}

void loop() {
  // Atiende HTTP + DNS del portal (si está activo) y maneja timeout
  wifiManager.update();

  // Estado real del enlace
  const bool wifiOk = wifiManager.isConnected();

  if (!wifiOk) {
    // Backoff interno (cada 10 s) + NTP al reconectar
    wifiManager.reintentarConexionSiNecesario();

    // Solo escanea cuando NO hay enlace (tiene anti-spam interno)
    if (wifiManager.scanRedDetectada()) {
      Serial.println("📶 Red preferida detectada. Intentando reconectar…");
      wifiManager.forzarReconexion();
    }

  } else {
    // Conectado: chequeo de internet y RSSI cada NET_CHECK_MS
    const uint32_t now = millis();
    if (now - lastNetCheck > NET_CHECK_MS) {
      lastNetCheck = now;
      const bool net  = wifiManager.hayInternet();
      const int  rssi = wifiManager.getSignalStrength();
      Serial.printf("🌍 Internet: %s | RSSI: %d dBm\n", net ? "OK" : "NO", rssi);
    }
  }

  delay(10); // respirito
}
