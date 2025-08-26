/*
 * SPDX-License-Identifier: MIT
 * Ejemplo — AyresWiFiManager (ESP32/ESP8266)
 * ---------------------------------------------------------------
 * @file       main.ino
 * @brief      Comportamiento de arranque:
 * - Con credenciales: portal 30 s (por INACTIVIDAD).
 * - Sin credenciales: portal “normal” (p. ej. 5 min).
 * En ambos casos: si lo usás, NO se cierra.
 *
 * Autor       : (Daniel Salgado) — AyresNet
 * Versión     : 2.0.0
 * Licencia    : MIT
 */

#include "AyresWiFiManager.h"

// ───────── Objetos ─────────────────────────────────────────────
AyresWiFiManager wifiManager;  // LED=2, BTN=0 (por defecto)

// ───────── Configuración de portales ───────────────────────────
static const uint32_t BOOT_PORTAL_S   = 30;   // 30 s de INACTIVIDAD (solo si hay credenciales)
static const uint32_t NORMAL_PORTAL_S = 300;  // 5 min “normal” (sin credenciales)

static uint32_t lastNetCheck = 0;
static const uint32_t NET_CHECK_MS = 30000;   // 30 s

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("==== AyresWiFiManager (pro) ====");

  // === RUTA DE HTML DEL PORTAL (si usás carpeta) ===
  // wifiManager.setHtmlPathPrefix("/wifimanager"); // ej: /wifimanager/index.html

  // === Hostname + SSID/Pass del AP del portal ===
  wifiManager.setHostname("ayreswifimanager");
  wifiManager.setAPCredentials("ayreswifimanager", "123456789");

  // Política de fallback (usamos el default o la que prefieras)
  // wifiManager.setFallbackPolicy(AyresWiFiManager::FallbackPolicy::BUTTON_ONLY);
  wifiManager.enableButtonPortal(true);

  // Monta FS, configura WiFi y carga /wifi.json si existe
  wifiManager.begin();

  // ------------------------------------------------------------
  //          Comportamiento según haya/NO haya credenciales
  // ------------------------------------------------------------
  if (wifiManager.tieneCredenciales()) {
    // ====== HAY credenciales: ventana de arranque 30 s ======
    // • DNS catch-all activo
    // • NO se cierra si hay cliente asociado al AP
    // • Cada request HTTP (p.ej. /scan) reinicia el contador
    wifiManager.setCaptivePortal(true);
    wifiManager.setPortalTimeout(BOOT_PORTAL_S);
    wifiManager.setAPClientCheck(true);
    wifiManager.setWebClientCheck(true);

    wifiManager.openPortal();
    Serial.printf("⏳ Ventana de arranque: portal activo (cierra tras %u s de inactividad)\n", BOOT_PORTAL_S);

    // Sin límite fijo: dejamos que update() cierre por INACTIVIDAD.
    while (wifiManager.isPortalActive()) {
      wifiManager.update();  // sirve HTTP/DNS y gestiona timeout
      // Si el usuario guarda credenciales (/save), el equipo se reinicia.
      delay(10);
    }

    Serial.println("✅ Ventana de arranque finalizada. Continuando…");

    // Restaurar timeout “normal” para usos futuros del portal
    wifiManager.setPortalTimeout(NORMAL_PORTAL_S);
    wifiManager.setAPClientCheck(true);
    wifiManager.setWebClientCheck(true);

    // Flujo estándar (detección botón, conectar, SMART, etc.)
    wifiManager.run();

  } else {
    // ====== NO hay credenciales: abrir portal “normal” ======
    // Queremos que el portal quede disponible para provisionar,
    // sin limitarlo a 30 s.
    wifiManager.setCaptivePortal(true);
    wifiManager.setPortalTimeout(NORMAL_PORTAL_S); // 5 min de INACTIVIDAD
    wifiManager.setAPClientCheck(true);
    wifiManager.setWebClientCheck(true);

    // Abrir YA el portal (sin esperar) y además correr el flujo estándar.
    // Si run() intenta abrir, startPortal() simplemente no duplicará.
    wifiManager.openPortal();
    Serial.println("🟡 Sin credenciales → portal de provisión abierto (5 min por inactividad).");

    wifiManager.run();  // muestra ventana de botón y mantiene la lógica general
  }

  // Autoreconexión + NTP al reconectar
  wifiManager.setAutoReconnect(true);

  const uint64_t ts = wifiManager.getTimestamp();
  if (ts > 0) {
    Serial.printf("⏱️ Timestamp (ms): %llu\n", (unsigned long long)ts);
  } else {
    Serial.println("⏱️ NTP no sincronizado aún.");
  }

  Serial.println("💡 Botón: 2–5 s abre portal, ≥5 s borra credenciales.");
}

void loop() {
  wifiManager.update();

  const bool wifiOk = wifiManager.isConnected();

  if (!wifiOk) {
    wifiManager.reintentarConexionSiNecesario();

    if (wifiManager.scanRedDetectada()) {
      Serial.println("📶 Red preferida detectada. Intentando reconectar…");
      wifiManager.forzarReconexion();
    }
  } else {
    const uint32_t now = millis();
    if (now - lastNetCheck > NET_CHECK_MS) {
      lastNetCheck = now;
      const bool net  = wifiManager.hayInternet();
      const int  rssi = wifiManager.getSignalStrength();
      Serial.printf("🌍 Internet: %s | RSSI: %d dBm\n", net ? "OK" : "NO", rssi);
    }
  }

  delay(10);
}