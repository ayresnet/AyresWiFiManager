/*
 *
 *  Support: If you find this useful, please ⭐ star the repository to help
 * others discover it.
 *
 *  OVERVIEW
 *  ---------------------------------------------------------------------------
 *  AyresWiFiManager is a production-grade Wi-Fi provisioning library for
 *  ESP32. It provides a captive portal (SoftAP + DNS catch-all),
 *  credential storage in LittleFS, resilient fallback policies, hardware UX
 *  (status LED + button), auto-reconnect, Internet reachability checks and
 *  robust time sync (NTP with rotation and timeouts; HTTP Date fallback on
 *  ESP32). Designed for reliability in consumer and fleet scenarios.
 *
 *  KEY FEATURES
 *  ---------------------------------------------------------------------------
 *  - Captive portal:
 *      SoftAP + DNS catch-all; static UI from LittleFS (index/success/error).
 *      JSON endpoints for scanning and device info.
 *  - Simple and safe provisioning:
 *      POST /save stores SSID/password in /wifi.json and reboots.
 *      POST /erase can remove only wifi.json or all JSON files.
 *  - Resilient connectivity:
 *      Fallback policies: NO_CREDENTIALS_ONLY (default), ON_FAIL,
 *      SMART_RETRIES, BUTTON_ONLY, NEVER.
 *      Configurable reconnect backoff and attempt window.
 *      Coexistence with an external AP without tearing down the internal AP.
 *  - Hardware UX:
 *      Status LED patterns (connected/portal/scan/feedback), and a single
 *      button: 2–5s opens portal, >=5s erases credentials and restarts.
 *  - Time and reachability:
 *      NTP sync with server rotation and timeouts.
 *      ESP32 HTTP Date fallback via settimeofday() if NTP is filtered.
 *      Internet check using HTTP 204 (generate_204).
 *
 *  HTTP ROUTES (served locally)
 *  ---------------------------------------------------------------------------
 *    GET  /                -> index.html
 *    POST /save            -> store credentials and reboot
 *    POST /erase           -> scope=wifi or scope=all; restarts on success
 *    GET  /scan or /scan.json
 *                           -> Wi-Fi list: [{ "ssid","rssi","secure" }]
 *    GET  /info            -> { name, version, version_code, ap, host, ap_ip }
 *    Captive redirectors: /generate_204, /gen_204, /hotspot-detect.html,
 *                         /connecttest.txt, /ncsi.txt, /fwlink  -> 302 to /
 *
 *  PUBLIC API — DETAILED REFERENCE
 *  ---------------------------------------------------------------------------
 *
 *  📌 CICLO DE VIDA
 *  ----------------
 *  begin()                     → Inicializa Wi-Fi, monta LittleFS y carga
 * credenciales. run()                       → Ejecuta flujo inicial: botón,
 * conexión, portal si aplica. update()                    → Llamado en loop();
 * maneja cliente HTTP, DNS, LED y timeouts.
 *
 *  📌 CONFIGURACIÓN DEL PORTAL Y AP
 *  --------------------------------
 *  setAPCredentials(ssid, pass) → Define SSID/clave del AP de configuración.
 *  setHostname(host)           → Nombre mDNS y hostname del dispositivo.
 *  setHtmlPathPrefix("/ui/")   → Prefijo para servir UI desde LittleFS (ej:
 * "/awm/"). setCaptivePortal(bool)      → Activa/desactiva redirección cautiva
 * (por defecto: true). setPortalTimeout(seconds)   → Cierra portal tras N
 * segundos sin actividad (0 = ilimitado). setAPClientCheck(bool)      →
 * Reinicia timeout si hay clientes conectados al AP. setWebClientCheck(bool) →
 * Reinicia timeout si hay tráfico HTTP (por defecto: true). openPortal() →
 * Activa AP, DNS y servidor HTTP (portal cautivo). closePortal() → Detiene DNS
 * y HTTP; AP persiste si setExternalApActive(true). isPortalActive() → Devuelve
 * true si el portal está activo.
 *
 *  📌 POLÍTICAS DE FALLBACK Y BOTÓN
 *  --------------------------------
 *  setFallbackPolicy(policy)   → Define cuándo abrir portal si falla conexión:
 *                                - ON_FAIL: siempre abre si falla.
 *                                - NO_CREDENTIALS_ONLY (default): solo si no
 * hay /wifi.json.
 *                                - SMART_RETRIES: abre tras N fallos en ventana
 * de tiempo.
 *                                - BUTTON_ONLY / NEVER: control manual o nunca.
 *  setSmartRetries(max, window)→ Configura SMART_RETRIES (ej: 3 fallos en 60s).
 *  enableButtonPortal(bool)    → Habilita apertura de portal con botón (2–5s).
 *
 *  📌 RECONEXIÓN Y CONTROL DE AP EXTERNO
 *  -------------------------------------
 *  setReconnectBackoffMs(ms)   → Mínimo tiempo entre intentos de reconexión
 * (default: 10s). setReconnectAttemptMs(ms)   → Duración máxima de cada intento
 * de conexión (default: 5s). setExternalApActive(bool)   → Si true,
 * closePortal() NO apaga el SoftAP (para coexistir con tu propio servidor
 * HTTP). isExternalApActive()        → Devuelve estado de AP externo.
 *  setAutoReconnect(bool)      → Activa/desactiva reconexión automática
 * (default: true). reintentarConexionSiNecesario() → Intenta reconectar
 * respetando backoff y políticas (ideal para loop). forzarReconexion() →
 * Reconecta inmediatamente, ignorando backoff (solo para acciones manuales).
 *
 *  📌 ESTADO Y UTILIDADES
 *  ----------------------
 *  isConnected()               → true si Wi-Fi está conectado (WL_CONNECTED).
 *  hayInternet()               → true si hay conectividad real (HTTP 204 a
 * Google). getSignalStrength()         → RSSI actual de la red Wi-Fi.
 *  getTimestamp()              → Hora sincronizada en ms desde Unix epoch (0 si
 * no sincronizada). tieneCredenciales()         → true si existe /wifi.json con
 * SSID/password válidos. scanRedDetectada()          → Escanea y devuelve true
 * si la red guardada está disponible.
 *
 *  📌 HARDWARE UX
 *  --------------
 *  setLedAuto(bool)            → Activa/desactiva gestión automática del LED
 * (default: true). setLedPatternManual(pat)    → Fuerza un patrón de LED (ON,
 * BLINK_SLOW, etc.).
 *
 *  📌 SEGURIDAD Y LIMPIEZA
 *  ------------------------
 *  setProtectedJsons({names})  → Protege archivos cuando /erase usa scope=all
 * sin force=1. El borrado total confirmado del portal usa force=1.
 *
 *  QUICK START
 *  ---------------------------------------------------------------------------
 *    @code
 *      #include <AyresWiFiManager.h>
 *      AyresWiFiManager wifi;
 *      void setup() {
 *        wifi.setAPCredentials("ayreswifimanager", "123456789");
 *        wifi.setPortalTimeout(300);
 *        wifi.setAPClientCheck(true);
 *        wifi.setWebClientCheck(true);
 *        wifi.begin();
 *        wifi.run();
 *      }
 *      void loop() { wifi.update(); }
 *    @endcode
 *
 *  DESIGN AND COMPATIBILITY
 *  ---------------------------------------------------------------------------
 *  ESP32: FreeRTOS timing (xTaskGetTickCount/vTaskDelay), esp_timer via
 *         AyresTimer for captive-portal timeout, Wi-Fi power save disabled.
 *         LittleFS.begin(true) auto-formats on mount failure.
 *
 *  PERFORMANCE AND OPERATIONS
 *  ---------------------------------------------------------------------------
 *  - Do not scan too frequently (SCAN_INTERVAL_MS).
 *  - Cache small JSON responses where appropriate.
 *  - Prefer scope=wifi. Use scope=all with force=1 only for a factory reset.
 *
 *  CHANGELOG (Semantic Versioning)
 *  ---------------------------------------------------------------------------
 *  2.3.0  (2026-08-07) ESP32-only release; unified connectivity state and
 *                      diagnostics; logger and documentation cleanup.
 *  2.2.0  (2025) NTP with rotation/timeouts; ESP32 HTTP Date fallback; TZ
 * "UTC0". 2.1.0  (2025) ESP32 moved to FreeRTOS ticks/vTaskDelay and esp_timer.
 *  2.0.x  (2024-2025) Fallback policies; reconnect tuning; JSON whitelist;
 *                     external AP support.
 *
 *  LEGAL
 *  ---------------------------------------------------------------------------
 *  MIT License. Use at your own risk. Review captive-portal regulations for
 *  your region if applicable.
 */

#ifndef AYRES_WIFI_MANAGER_H
#define AYRES_WIFI_MANAGER_H

// ===== Versioning (public) =====
#define AWM_VERSION "2.3.1"
#define AWM_VERSION_MAJOR 2
#define AWM_VERSION_MINOR 3
#define AWM_VERSION_PATCH 1

#include <Arduino.h>

#if defined(ESP32)
#include <WebServer.h>
#include <WiFi.h>
#include <esp_timer.h>
#else
#error "AyresWiFiManager 2.3.0 supports ESP32 only"
#endif

#include <DNSServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <functional> // Necesario para std::function
#include <initializer_list>
#include <vector>

class AyresWiFiManager {
public:
  // ---------- utilidades de versión ----------
  static inline const char *versionString() { return AWM_VERSION; }
  static constexpr uint32_t versionCode() {
    return (AWM_VERSION_MAJOR << 16) | (AWM_VERSION_MINOR << 8) |
           AWM_VERSION_PATCH;
  }

  // ---------- políticas de fallback ----------
  enum class FallbackPolicy : uint8_t {
    ON_FAIL,
    NO_CREDENTIALS_ONLY, // DEFAULT
    SMART_RETRIES,
    BUTTON_ONLY,
    NEVER
  };

  // ---------- patrones del LED ----------
  enum class LedPattern : uint8_t {
    OFF,
    ON,
    BLINK_SLOW,
    BLINK_FAST,
    BLINK_DOUBLE,
    BLINK_TRIPLE
  };

  // Estado unificado para diagnóstico de conectividad.
  // PORTAL_ACTIVE tiene prioridad mientras el portal está abierto.
  enum class State : uint8_t {
    OFFLINE,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    INTERNET_OK,
    NO_INTERNET,
    PORTAL_ACTIVE
  };

  enum class Error : uint8_t {
    NONE,
    NO_CREDENTIALS,
    CONNECTION_TIMEOUT,
    INTERNET_UNREACHABLE,
    STORAGE_ERROR,
    ENCRYPTION_ERROR
  };

  // ---------- ctor ----------
  AyresWiFiManager(uint8_t ledPin = 2, uint8_t buttonPin = 0);

  // ---------- ciclo de vida ----------
  void begin();
  void run();
  void update();

  // Devuelve la MAC completa de la interfaz Station, por ejemplo
  // "A4:CF:12:34:4C:25".
  static String getMacAddress();
  // Devuelve los ultimos cuatro caracteres hexadecimales, por ejemplo "4C25".
  static String getMacSuffix();

  // ---------- configuración de portal/AP ----------
  void setHtmlPathPrefix(const String &prefix);
  void setHostname(const String &host);
  void setAPCredentials(const String &ssid, const String &pass);
  // Reutiliza internamente la clave Wi-Fi para el SoftAP sin exponerla.
  // Devuelve false si no hay una clave WPA valida (8 a 63 caracteres).
  bool setAPCredentialsUsingStoredPassword(const String &ssid);
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
  int getSignalStrength();
  uint64_t getTimestamp();
  bool connectToWiFi();
  void reintentarConexionSiNecesario();
  bool hayInternet();
  bool tieneCredenciales() const;
  // Comprueba una clave sin devolver la credencial almacenada.
  bool verifyWiFiPassword(const String &candidate) const;
  // Compatibilidad heredada. Preferir verifyWiFiPassword().
  String getWiFiPass() const;

  // ---------- diagnóstico ----------
  State getState() const;
  static const char *stateToString(State state);
  Error getLastError() const;
  static const char *errorToString(Error error);
  uint32_t getReconnectCount() const;
  uint32_t getLastInternetCheck() const;

  // ---------- utilidades extra ----------
  bool scanRedDetectada();
  void forzarReconexion();

  // ---------- LED ----------
  void setLedAuto(bool enable);
  void setLedPatternManual(LedPattern p);

  // ---------- lista blanca ----------
  void setProtectedJsons(std::initializer_list<const char *> names);

  // ==== NUEVO: control de reconexión y AP externo ====
  void setReconnectBackoffMs(uint32_t ms);
  void setReconnectAttemptMs(uint32_t ms);
  void setExternalApActive(bool active);
  bool isExternalApActive() const;

  // ==== Busy Callback (para mantener la UI viva durante bloqueos) ====
  void setBusyCallback(std::function<void()> cb);

  // ==== Credential Encryption (AES-128-GCM) ====
  bool setCredentialEncryption(bool enabled, const char *aes_key = nullptr);
  void enableCredentialEncryption(const char *aes_key); // 16 bytes for AES-128
  void disableCredentialEncryption();
  bool isEncryptionEnabled() const;

private:
  // ---------- portal AP/DNS/HTTP ----------
  void setupAP();
  void startPortal();
  void stopPortal();
  void setupHTTPRoutes();
  void startDNS();
  void stopDNS();
  bool captivePortalRedirect();
  void redirectToRoot();
  uint8_t softAPStationCount();

  // HTTP handlers
  void handleRoot();
  void handleSave();
  void handleScan();
  void handleNotFound();
  void mostrarPaginaError(const String &mensajeFallback);
  void handleErase();

#if defined(ESP32)
  void restartPortalTimeout();
  // Native esp_timer helpers
  void createTimers();
  static void IRAM_ATTR _onPortalTimerCallback(void *arg);
  static void IRAM_ATTR _onScanTimerCallback(void *arg);
#endif

  // ---------- credenciales ----------
  void loadCredentials();
  bool saveCredentials(String ssid, String password);
  void eraseCredentials();
  bool eraseWiFiCredentials();
  struct EraseResult {
    uint16_t found = 0;
    uint16_t removed = 0;
    uint16_t failed = 0;
  };
  bool isProtectedJson(const String &name) const;
  void eraseJsonInDir(const char *path, bool respectProtected,
                      EraseResult &result);

  // ---------- NTP ----------
  void sincronizarHoraNTP();

  // ---------- LED FSM ----------
  void ledAutoUpdate();
  void ledTask();
  void ledSet(LedPattern p);

  // ---------- datos ----------
  // credenciales y HTML
  String ssid, password;
  String htmlPathPrefix = "/";

  // servidor / dns
  WebServer server{80};
  DNSServer dns;
  bool portalActive = false;
  bool dnsRunning = false;

  // AP config
  IPAddress apIP{192, 168, 4, 1}, apGW{192, 168, 4, 1}, apSN{255, 255, 255, 0};
  String hostname;
  String apSSID = "WiFi Manager";
  String apPASS = "123456789";

  // portal behaviour
  bool captiveEnabled = true;
  uint32_t portalTimeoutMs = 0; // 0 = sin timeout
  bool apClientCheck = false;
  bool webClientCheck = true;

  // tiempos
  unsigned long portalStart = 0;
  unsigned long lastHttpAccess = 0;

  // fallback
  FallbackPolicy fallbackPolicy = FallbackPolicy::NO_CREDENTIALS_ONLY;
  bool allowButtonPortal = true;
  uint8_t maxFailRetries = 3;
  uint32_t failWindowMs = 60000;
  uint8_t failCount = 0;
  unsigned long failWindowStart = 0;

  // conexión
  bool connected = false;
  bool autoReconnect = true;
  unsigned long ultimoIntentoWiFi = 0;
  State _state = State::OFFLINE;
  Error _lastError = Error::NONE;
  uint32_t _reconnectCount = 0;
  uint32_t _lastInternetCheck = 0;

  // scan helper
  unsigned long ultimoScan = 0;
  static constexpr unsigned long SCAN_INTERVAL_MS = 15000;
  static constexpr unsigned long SCAN_CACHE_MS = 1500;
  String lastScanJson;
  unsigned long lastScanAt = 0;
  bool scanning = false;
  unsigned long scanningUntil = 0;

  // GPIO
  uint8_t ledPin, buttonPin;

  // LED FSM
  bool ledAuto = true;
  LedPattern ledPat = LedPattern::OFF;
  uint8_t ledOut = LOW;
  uint8_t ledStep = 0;
  unsigned long ledT0 = 0;

  // Lista blanca exacta
  std::vector<String> _protectedExact;

  // Parámetros de reconexión
  uint32_t reconnectBackoffMs = 10000;
  uint32_t reconnectAttemptMs = 5000;

  // AP externo
  bool externalApActive = false;

  // Credential encryption
  bool _encryptionEnabled = false;
  bool _aesKeyConfigured = false;
  uint8_t _aesKey[16]; // AES-128 key
  String encryptCredentialEnvelope(const String &ssid, const String &password);
  bool decryptCredentialEnvelope(const String &envelope, String &ssid,
                                 String &password);
  // Lectura exclusiva de formatos cifrados anteriores a AWM 2.3.0.
  String decryptString(const String &ciphertext);
  String base64Encode(const uint8_t *data, size_t len);
  bool base64Decode(const String &b64, uint8_t *out, size_t *outLen);

#if defined(ESP32)
  // ---- Timers ESP32 (Native esp_timer) ----
  esp_timer_handle_t _portalTimer = nullptr;
  esp_timer_handle_t _scanTimer = nullptr;

  volatile bool _portalTimeoutExpired = false;
#endif

  // Callback de actividad (Busy Loop)
  std::function<void()> _busyCallback = nullptr;
};

#endif // AYRES_WIFI_MANAGER_H
