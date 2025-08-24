/* 
 *  SPDX-License-Identifier: MIT
 *  AyresWiFiManager — Header
 *  ---------------------------------------------------------------
 *  @file      AyresWiFiManager.h
 *  @brief     Gestor Wi-Fi “pro” para ESP32/ESP8266 con portal cautivo,
 *             almacenamiento de credenciales en LittleFS, escaneo JSON,
 *             LED de estado, botón de provisión y utilidades extra.
 *
 *  @details
 *  • Almacena credenciales en /wifi.json (LittleFS).
 *  • Portal cautivo AP+DNS (opcional) sirviendo HTML desde el FS:
 *      - GET  /            → index.html
 *      - POST /save        → guarda ssid/password y reinicia
 *      - GET  /scan(.json) → listado de redes [{ssid,rssi,secure}]
 *  • “Captive Portal” real: redirección de dominios comunes (Android/iOS/Win)
 *    y DNS catch-all a 192.168.4.1 (desactivable con setCaptivePortal(false)).
 *  • Políticas de fallback: NO_CREDENTIALS_ONLY, ON_FAIL, SMART_RETRIES,
 *    BUTTON_ONLY y NEVER.
 *  • Botón (activo en LOW):
 *      - 2–5 s → abre portal
 *      - ≥5 s  → borra .json (respetando lista blanca) y reinicia
 *  • LED de estado (pin por defecto 2):
 *      Conectado→ON | Portal→BLINK_SLOW | Escaneo→BLINK_FAST | Idle→OFF
 *  • NTP (pool.ntp.org / time.nist.gov) y verificación de Internet (generate_204).
 *  • v2.0.0: Lista blanca de .json configurable desde el sketch:
 *      setProtectedJsons({"wifi.json","licencia.json", ...})
 *
 *  Compatibilidad
 *  ---------------------------------------------------------------
 *  • ESP32 (Arduino core) — <WiFi.h>, <WebServer.h>, HTTPClient, LittleFS.
 *  • ESP8266 (Arduino core) — <ESP8266WiFi.h>, <ESP8266WebServer.h>,
 *    ESP8266HTTPClient y LittleFS.
 *
 *  Uso mínimo
 *  ---------------------------------------------------------------
 *  @code
 *    AyresWiFiManager wifi;
 *    void setup() {
 *      wifi.setAPCredentials("ayreswifimanager","123456789");
 *      wifi.setPortalTimeout(300);       // 5 min de inactividad
 *      wifi.setAPClientCheck(true);      // no cierra si hay clientes
 *      wifi.setWebClientCheck(true);     // cada request reinicia timer
 *      // wifi.setCaptivePortal(false);  // si querés SIN redirecciones
 *      wifi.begin();
 *      wifi.run();                       // intenta STA o aplica política
 *    }
 *    void loop() { wifi.update(); }
 *  @endcode
 *
 *  Autor       : (Daniel Salgado) — AyresNet
 *  Versión     : 2.0.0
 *  Licencia    : MIT
 *  Proyecto    : https://github.com/AyresNet/AyresWiFiManager
 *  (C) 2025 AyresNet. Todos los derechos reservados bajo licencia MIT.
 */

#ifndef AYRES_WIFI_MANAGER_H
#define AYRES_WIFI_MANAGER_H

#include <Arduino.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #define WebServer ESP8266WebServer
#else
  #error "Plataforma no soportada (ESP32 o ESP8266)"
#endif

#include <FS.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include <vector>
#include <initializer_list>

/**
 * @class AyresWiFiManager
 * @brief Gestión Wi-Fi profesional con:
 *  - Credenciales en LittleFS (/wifi.json)
 *  - Portal cautivo (AP+DNS) con HTML servido desde FS
 *  - Escaneo JSON en /scan
 *  - Políticas de fallback configurables
 *  - Botón: 2–5s abre portal / ≥5s borra credenciales
 *  - NTP y chequeo de Internet (generate_204)
 *  - Indicador LED por estados (conectado, portal, escaneo, etc.)
 */
class AyresWiFiManager {
public:
    // ---------- políticas de fallback ----------
    enum class FallbackPolicy : uint8_t {
        ON_FAIL,
        NO_CREDENTIALS_ONLY,  // DEFAULT
        SMART_RETRIES,
        BUTTON_ONLY,
        NEVER
    };

    // ---------- patrones del LED ----------
    enum class LedPattern : uint8_t {
        OFF, ON, BLINK_SLOW, BLINK_FAST, BLINK_DOUBLE, BLINK_TRIPLE
    };

    // ---------- ctor ----------
    AyresWiFiManager(uint8_t ledPin = 2, uint8_t buttonPin = 0);

    // ---------- ciclo de vida ----------
    void begin();
    void run();
    void update();

    // ---------- configuración de portal/AP ----------
    void setHtmlPathPrefix(const String& prefix);
    void setHostname(const String& host);
    void setAPCredentials(const String& ssid, const String& pass);
    void setCaptivePortal(bool enabled);
    void setPortalTimeout(uint32_t seconds);
    void setAPClientCheck(bool enabled);
    void setWebClientCheck(bool enabled);
    void openPortal();
    void closePortal();
    bool isPortalActive() const;

    // ---------- fallback ----------
    void setFallbackPolicy(FallbackPolicy p);
    void setSmartRetries(uint8_t maxRetries, uint32_t windowMs);
    void enableButtonPortal(bool enable);
    void setAutoReconnect(bool habilitado);

    // ---------- utilidades ----------
    bool isConnected();
    int  getSignalStrength();
    uint64_t getTimestamp();
    bool connectToWiFi();
    void reintentarConexionSiNecesario();
    bool hayInternet();
    bool tieneCredenciales() const;

    // ---------- utilidades extra ----------
    bool scanRedDetectada();
    void forzarReconexion();

    // ---------- LED ----------
    void setLedAuto(bool enable);
    void setLedPatternManual(LedPattern p);

    // ---------- ÚNICO método público para lista blanca ----------
    // Pasá uno o varios nombres (con o sin '/'). Ejemplo:
    // wifiManager.setProtectedJsons({"/licencia.json","secret.json"});
    void setProtectedJsons(std::initializer_list<const char*> names);

private:
    // ---------- portal AP/DNS/HTTP ----------
    void setupAP();
    void startPortal();
    void stopPortal();
    void setupHTTPRoutes();
    void startDNS();
    void stopDNS();
    bool captivePortalRedirect();
    bool portalHasTimedOut();
    void redirectToRoot();
    uint8_t softAPStationCount();

    // HTTP handlers
    void handleRoot();
    void handleSave();
    void handleScan();
    void handleNotFound();
    void mostrarPaginaError(const String& mensajeFallback);
    void handleErase();  // nueva linea para eliminar desde el sitio.

    // ---------- credenciales ----------
    void loadCredentials();
    void saveCredentials(String ssid, String password);
    void eraseCredentials();
    bool isProtectedJson(const String& name) const;
    void eraseJsonInDir(const char* path);

    // ---------- NTP ----------
    void sincronizarHoraNTP();

    // ---------- LED FSM ----------
    void ledAutoUpdate();
    void ledTask();
    void ledSet(LedPattern p);

    // ---------- datos ----------
    // credenciales y HTML
    String ssid, password;
    String htmlPathPrefix = "/";   // raíz del FS por defecto

    // servidor / dns
    WebServer server{80};
    DNSServer dns;
    bool portalActive      = false;
    bool dnsRunning        = false;

    // AP config
    IPAddress apIP{192,168,4,1}, apGW{192,168,4,1}, apSN{255,255,255,0};
    String hostname;
    String apSSID = "WiFi Manager";
    String apPASS = "123456789";

    // portal behaviour
    bool     captiveEnabled  = true;
    uint32_t portalTimeoutMs = 0;   // 0 = sin timeout
    bool     apClientCheck   = false;
    bool     webClientCheck  = true;
    unsigned long portalStart = 0;
    unsigned long lastHttpAccess = 0;

    // fallback
    FallbackPolicy fallbackPolicy = FallbackPolicy::NO_CREDENTIALS_ONLY;
    bool allowButtonPortal = true;
    uint8_t  maxFailRetries = 3;
    uint32_t failWindowMs   = 60000;
    uint8_t  failCount      = 0;
    unsigned long failWindowStart = 0;

    // conexión
    bool connected = false;
    bool autoReconnect = true;
    unsigned long ultimoIntentoWiFi = 0;

    // scan helper
    unsigned long ultimoScan = 0;
    static constexpr unsigned long SCAN_INTERVAL_MS = 15000;
    static constexpr unsigned long SCAN_CACHE_MS    = 1500; // (si usás cache en el futuro)
    String lastScanJson;
    unsigned long lastScanAt = 0;
    bool scanning = false;
    unsigned long scanningUntil = 0;

    // GPIO
    uint8_t ledPin, buttonPin;

    // LED FSM
    bool        ledAuto = true;
    LedPattern  ledPat  = LedPattern::OFF;
    uint8_t     ledOut  = LOW;
    uint8_t     ledStep = 0;
    unsigned long ledT0 = 0;

    // Lista blanca exacta (nombres de archivo)
    std::vector<String> _protectedExact;
};

#endif // AYRES_WIFI_MANAGER_H
