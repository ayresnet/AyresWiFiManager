/* 
 *  SPDX-License-Identifier: MIT
 *  AyresWiFiManager — Public API (Header)
 *  ---------------------------------------------------------------
 *  @file      AyresWiFiManager.h
 *  @version   2.0.2
 *  @author    Daniel C. Salgado — AyresNet
 *  @license   MIT
 *  @project   https://github.com/AyresNet/AyresWiFiManager
 *
 *  @brief  Professional Wi-Fi manager for ESP32/ESP8266 featuring:
 *          • Captive portal (AP + DNS catch-all) for provisioning
 *          • Credentials stored in LittleFS (/wifi.json)
 *          • JSON scan endpoint (/scan or /scan.json)
 *          • Fallback policies + auto-reconnect
 *          • Provisioning button (short/long press actions)
 *          • Status LED patterns
 *          • Optional NTP sync and Internet reachability check (generate_204)
 *
 *  Key HTTP routes (served from LittleFS):
 *    - GET  /             → index.html
 *    - POST /save         → store SSID/password and restart
 *    - GET  /scan(.json)  → Wi-Fi list [{ssid,rssi,secure}]
 *    - GET  /erase        → wipe stored credentials (respects whitelist)
 *
 *  Fallback policies:
 *    - NO_CREDENTIALS_ONLY (default) | ON_FAIL | SMART_RETRIES | BUTTON_ONLY | NEVER
 *
 *  Button (active LOW, configurable):
 *    - 2–5 s  → open captive portal
 *    - ≥5 s   → erase credentials (respecting whitelist) and restart
 *
 *  Status LED (default pin 2):
 *    - Connected → ON | Portal → BLINK_SLOW | Scan → BLINK_FAST | Idle → OFF
 *
 *  v2.0.1 highlights:
 *    - Configurable reconnect backoff and attempt windows
 *    - JSON whitelist via setProtectedJsons({...})
 *    - External AP/portal coexistence flag:
 *        setExternalApActive(true) keeps SoftAP in AP+STA scenarios and
 *        prevents unintended portal shutdown while an external AP/portal is active
 *
 *  Compatibility:
 *    - ESP32 Arduino core: <WiFi.h>, <WebServer.h>, HTTPClient, LittleFS
 *    - ESP8266 Arduino core: <ESP8266WiFi.h>, <ESP8266WebServer.h>, ESP8266HTTPClient, LittleFS
 *
 *  Minimal usage:
 *    @code
 *      #include <AyresWiFiManager.h>
 *      AyresWiFiManager wifi;
 *      void setup() {
 *        wifi.setAPCredentials("ayreswifimanager","123456789");
 *        wifi.setPortalTimeout(300);      // 5 min inactivity
 *        wifi.setAPClientCheck(true);     // keep portal if AP clients exist
 *        wifi.setWebClientCheck(true);    // reset timeout on HTTP requests
 *        // wifi.setCaptivePortal(false); // disable DNS redirection if needed
 *        wifi.begin();
 *        wifi.run();                      // try STA or fallback to portal
 *      }
 *      void loop() { wifi.update(); }
 *    @endcode
 */


#ifndef AYRES_WIFI_MANAGER_H
#define AYRES_WIFI_MANAGER_H

// ===== Versioning (public) =====
#define AWM_VERSION        "2.0.2"
#define AWM_VERSION_MAJOR  2
#define AWM_VERSION_MINOR  0
#define AWM_VERSION_PATCH  2

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

    // ==== NUEVO: control de reconexión y AP externo ====
    void setReconnectBackoffMs(uint32_t ms);   // [NEW]
    void setReconnectAttemptMs(uint32_t ms);   // [NEW]
    void setExternalApActive(bool active);     // [NEW]
    bool isExternalApActive() const;           // [NEW]

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

    // [NEW] Parámetros de reconexión configurables
    uint32_t reconnectBackoffMs = 10000;  // default 10s (antes fijo)
    uint32_t reconnectAttemptMs = 5000;   // default 5s  (antes fijo)

    // [NEW] Bandera para indicar que hay un AP/portal externo activo
    bool externalApActive = false;        // usado para mantener AP_STA en reintentos
};

#endif // AYRES_WIFI_MANAGER_H
