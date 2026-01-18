/*
 *  AyresWiFiManager — Firmware Library
 *  =========================================================================
 *  Archivo   : AyresWiFiManager.cpp
 *  Versión   : 2.2.1
 *  Autor     : Daniel C. Salgado
 *  Empresa   : AyresNet IoT Systems
 *  Repositorio: https://github.com/ayresnet/AyresWiFiManager
 *
 *  © 2025 AyresNet IoT Systems. Todos los derechos reservados.
 *  Licencia  : MIT License
 *
 *  DESCRIPCIÓN
 *  =========================================================================
 *  Gestor profesional de conectividad WiFi y provisión para ESP32/ESP8266.
 *  Características principales:
 *
 *  - Portal Cautivo completo (SoftAP + DNS catch-all)
 *  - UI moderna servida desde LittleFS (index/success/error.html)
 *  - Almacenamiento seguro de credenciales en /wifi.json
 *  - Autoconexión y reconexión inteligente (Smart Retries)
 *  - Sincronización de hora NTP con rotación y fallback HTTP Date (ESP32)
 *  - API REST local (/scan, /save, /erase, /info)
 *  - Control por hardware: LED de estado + botón físico
 *  - Políticas de fallback configurables
 *  - Verificación de conectividad real a Internet
 *  - Soporte completo para ESP32 y ESP8266
 *
 *  CHANGELOG (Semantic Versioning)
 *  =========================================================================
 *  v2.2.1 (2025-12-15)
 *    + [FIX] Compatibilidad ESP8266: AUTH_OPEN vs WIFI_AUTH_OPEN en
 * handleScan() Issue reportado en GitHub - ahora compila correctamente en ambas
 * plataformas
 *    + [DOC] Encabezados mejorados con changelog completo
 *
 *  v2.2.0 (2025)
 *    + [FEATURE] Sincronización NTP con rotación de servidores y timeouts
 *    + [FEATURE] Fallback HTTP Date en ESP32 usando settimeofday()
 *    + [CHANGE] Zona horaria forzada a "UTC0" para consistencia
 *    + [IMPROVE] Mejor manejo de timeouts en sincronización de hora
 *
 *  v2.1.0 (2025)
 *    + [CHANGE] ESP32: migrado a FreeRTOS ticks (xTaskGetTickCount/vTaskDelay)
 *    + [CHANGE] ESP32: uso de esp_timer nativo para timeouts de portal
 *    + [IMPROVE] Mejor precisión en timeouts y delays en ESP32
 *
 *  v2.0.x (2024-2025)
 *    + [FEATURE] Políticas de fallback (ON_FAIL, NO_CREDENTIALS_ONLY, etc.)
 *    + [FEATURE] Parámetros configurables de reconexión (backoff, attempt
 * window)
 *    + [FEATURE] Lista blanca de archivos JSON protegidos
 *    + [FEATURE] Soporte para AP externo persistente
 *    + [IMPROVE] Arquitectura de reconexión mejorada
 *
 *  PLATAFORMAS SOPORTADAS
 *  =========================================================================
 *  ESP32:   FreeRTOS timing, esp_timer, auto-format LittleFS, WiFi PS_NONE
 *  ESP8266: millis()/delay() timing, timeout clásico, LittleFS manual mount
 *
 *  DEPENDENCIAS
 *  =========================================================================
 *  - ArduinoJson >= 6.21.2
 *  - LittleFS (incluido en framework)
 *  - DNSServer (incluido en framework)
 *  - WebServer/ESP8266WebServer (incluido en framework)
 *
 *  DOCUMENTACIÓN COMPLETA
 *  =========================================================================
 *  Ver archivo AyresWiFiManager.h para API completa y ejemplos de uso.
 */

#include "AyresWiFiManager.h"
#include "AWM_Logging.h"
#include "AWM_html_gz.h"
#include "AyresWiFiManager.h"
#include <ArduinoJson.h>

// Mapping logic to standard logging
#define AYLOG_E AWM_LOGE
#define AYLOG_W AWM_LOGW
#define AYLOG_I AWM_LOGI
#define AYLOG_D AWM_LOGD
#define AYLOG_V AWM_LOGV

#if defined(ESP32)
#include <HTTPClient.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/time.h> // settimeofday (fallback HTTP Date)
#elif defined(ESP8266)
#include <ESP8266HTTPClient.h>
#endif

// mbedTLS for AES encryption
#if defined(ESP32)
#include "esp_system.h" // for esp_random()
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"

#elif defined(ESP8266)
#include "bearssl/bearssl.h" // ESP8266 uses BearSSL
extern "C" {
#include "user_interface.h"  // for os_random()
}
#endif

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ===== Helpers de tiempo (abstraen millis/delay) ===== */
#if defined(ESP32)
static inline uint32_t AWM_now_ms() {
  return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
static inline void AWM_sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
#else
static inline uint32_t AWM_now_ms() { return millis(); }
static inline void AWM_sleep_ms(uint32_t ms) { delay(ms); }
#endif

/* ============================== Helpers NTP/Fecha
 * =============================== */

// Espera a que el tiempo esté disponible.
// - ESP32: usa getLocalTime(&tm, timeoutMs)
// - Otros: sonda time(nullptr) hasta timeout
static bool AWM_waitLocalTime_(struct tm *ti, uint32_t timeoutMs) {
#if defined(ESP32)
  return getLocalTime(ti, timeoutMs);
#else
  const uint32_t step = 200;
  uint32_t waited = 0;
  while (waited < timeoutMs) {
    time_t now = time(nullptr);
    if (now > 100000) { // umbral razonable
      if (ti)
        localtime_r(&now, ti);
      return true;
    }
    AWM_sleep_ms(step);
    waited += step;
  }
  return false;
#endif
}

#if defined(ESP32)
// Parsea "Sat, 27 Sep 2025 04:35:06 GMT" -> epoch (UTC)
// Importante: asumimos TZ=UTC0 (ya seteado por sincronizarHoraNTP con
// configTzTime)
static bool AWM_parseHttpDate_(const String &date, time_t *outEpoch) {
  if (!outEpoch || date.length() < 29)
    return false;
  char wdy[4] = {0}, mon[4] = {0}, tz[4] = {0};
  int d = 0, y = 0, H = 0, M = 0, S = 0;
  if (sscanf(date.c_str(), "%3s, %d %3s %d %d:%d:%d %3s", wdy, &d, mon, &y, &H,
             &M, &S, tz) != 8)
    return false;

  const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char *p = strstr(months, mon);
  if (!p)
    return false;
  int monthIdx = (int)((p - months) / 3);

  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = monthIdx;
  t.tm_mday = d;
  t.tm_hour = H;
  t.tm_min = M;
  t.tm_sec = S;
  t.tm_isdst = 0;

  // Usamos mktime asumiendo TZ=UTC0 (equivalente a timegm en este contexto)
  time_t epoch = mktime(&t);
  if (epoch <= 0)
    return false;
  *outEpoch = epoch;
  return true;
}

// Fallback: sincroniza desde cabecera HTTP Date (sin TLS)
static bool AWM_syncTimeFromHttp_(const char *url, uint32_t timeoutMs = 6000) {
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url))
    return false;

  const char *hdrs[] = {"Date"};
  http.collectHeaders(hdrs, 1);
  http.setConnectTimeout(timeoutMs);

  int code = http.GET();
  String date = http.header("Date");
  http.end();

  if (code <= 0 || !date.length())
    return false;

  time_t epoch = 0;
  if (!AWM_parseHttpDate_(date, &epoch))
    return false;

  struct timeval tv{.tv_sec = epoch, .tv_usec = 0};
  settimeofday(&tv, nullptr);

  struct tm ti{};
  localtime_r(&epoch, &ti);
  char buf[32];
  strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", &ti);
  AYLOG_I("🕒 Hora desde HTTP: %s", buf);
  return true;
}
#endif // ESP32

/* ============================ ctor / setters ============================ */

AyresWiFiManager::AyresWiFiManager(uint8_t ledPin_, uint8_t buttonPin_)
    : server(80), ledPin(ledPin_), buttonPin(buttonPin_) {}

/* ============================= SETTERS / TOGGLES =============================
 */
void AyresWiFiManager::setHtmlPathPrefix(const String &prefix) {
  htmlPathPrefix = prefix.endsWith("/") ? prefix : prefix + "/";
}
void AyresWiFiManager::setHostname(const String &host) { hostname = host; }
void AyresWiFiManager::setAPCredentials(const String &ssid_,
                                        const String &pass_) {
  apSSID = ssid_;
  apPASS = pass_;
}

void AyresWiFiManager::setCaptivePortal(bool enabled) {
  captiveEnabled = enabled;
}
void AyresWiFiManager::setPortalTimeout(uint32_t seconds) {
  portalTimeoutMs = seconds * 1000UL;
}
void AyresWiFiManager::setAPClientCheck(bool enabled) {
  apClientCheck = enabled;
}
void AyresWiFiManager::setWebClientCheck(bool enabled) {
  webClientCheck = enabled;
}
bool AyresWiFiManager::isPortalActive() const { return portalActive; }
void AyresWiFiManager::openPortal() { startPortal(); }
void AyresWiFiManager::closePortal() { stopPortal(); }

void AyresWiFiManager::setFallbackPolicy(FallbackPolicy p) {
  fallbackPolicy = p;
}
void AyresWiFiManager::setSmartRetries(uint8_t maxRetries, uint32_t windowMs) {
  maxFailRetries = maxRetries;
  failWindowMs = windowMs;
}
void AyresWiFiManager::enableButtonPortal(bool enable) {
  allowButtonPortal = enable;
}

/* ===== Reconexión configurable ===== */
void AyresWiFiManager::setReconnectBackoffMs(uint32_t ms) {
  reconnectBackoffMs = (ms < 1000) ? 1000 : ms;
  AYLOG_I("⚙️  Backoff de reconexión = %lu ms",
          (unsigned long)reconnectBackoffMs);
}
void AyresWiFiManager::setReconnectAttemptMs(uint32_t ms) {
  reconnectAttemptMs = (ms < 1000) ? 1000 : ms;
  AYLOG_I("⚙️  Ventana de intento = %lu ms", (unsigned long)reconnectAttemptMs);
}
void AyresWiFiManager::setExternalApActive(bool active) {
  externalApActive = active;
  AYLOG_I("⚙️  AP externo activo: %s", externalApActive ? "sí" : "no");
}
bool AyresWiFiManager::isExternalApActive() const { return externalApActive; }

void AyresWiFiManager::setBusyCallback(std::function<void()> cb) {
  _busyCallback = cb;
}

/* ================================= CREDENTIAL ENCRYPTION (AES-128)
 * ================================ */
void AyresWiFiManager::enableCredentialEncryption(const char *aes_key) {
  if (!aes_key || strlen(aes_key) != 16) {
    AYLOG_E("❌ AES key must be exactly 16 bytes");
    return;
  }
  memcpy(_aesKey, aes_key, 16);
  _encryptionEnabled = true;
  AYLOG_I("🔒 Credential encryption enabled");
}

void AyresWiFiManager::disableCredentialEncryption() {
  _encryptionEnabled = false;
  memset(_aesKey, 0, 16); // Clear key from memory
  AYLOG_I("🔓 Credential encryption disabled");
}

bool AyresWiFiManager::isEncryptionEnabled() const {
  return _encryptionEnabled;
}

String AyresWiFiManager::base64Encode(const uint8_t *data, size_t len) {
  size_t olen = 0;
#if defined(ESP32)
  mbedtls_base64_encode(NULL, 0, &olen, data, len); // Get required size
  uint8_t *buf = (uint8_t *)malloc(olen);
  if (!buf)
    return "";

  if (mbedtls_base64_encode(buf, olen, &olen, data, len) == 0) {
    String result = String((char *)buf);
    free(buf);
    return result;
  }
  free(buf);
#elif defined(ESP8266)
  // ESP8266 doesn't have mbedtls_base64, use manual implementation
  const char *b64chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String result;
  result.reserve((len * 4 / 3) + 4);

  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = ((uint32_t)data[i]) << 16;
    if (i + 1 < len)
      n |= ((uint32_t)data[i + 1]) << 8;
    if (i + 2 < len)
      n |= data[i + 2];

    result += b64chars[(n >> 18) & 0x3F];
    result += b64chars[(n >> 12) & 0x3F];
    result += (i + 1 < len) ? b64chars[(n >> 6) & 0x3F] : '=';
    result += (i + 2 < len) ? b64chars[n & 0x3F] : '=';
  }
  return result;
#endif
  return "";
}

bool AyresWiFiManager::base64Decode(const String &b64, uint8_t *out,
                                    size_t *outLen) {
#if defined(ESP32)
  return (mbedtls_base64_decode(out, *outLen, outLen,
                                (const uint8_t *)b64.c_str(),
                                b64.length()) == 0);
#elif defined(ESP8266)
  // Manual Base64 decode for ESP8266
  const char *input = b64.c_str();
  size_t len = b64.length();
  size_t olen = 0;

  for (size_t i = 0; i < len; i++) {
    char c = input[i];
    if (c == '=')
      break;

    uint8_t v;
    if (c >= 'A' && c <= 'Z')
      v = c - 'A';
    else if (c >= 'a' && c <= 'z')
      v = c - 'a' + 26;
    else if (c >= '0' && c <= '9')
      v = c - '0' + 52;
    else if (c == '+')
      v = 62;
    else if (c == '/')
      v = 63;
    else
      continue;

    static uint32_t buf = 0;
    static int bits = 0;

    buf = (buf << 6) | v;
    bits += 6;

    if (bits >= 8) {
      bits -= 8;
      if (olen < *outLen) {
        out[olen++] = (buf >> bits) & 0xFF;
      }
    }
  }
  *outLen = olen;
  return true;
#endif
}

String AyresWiFiManager::encryptString(const String &plaintext) {
  if (plaintext.isEmpty())
    return "";

  // Generate random IV
  uint8_t iv[16];
#if defined(ESP32)
  for (int i = 0; i < 16; i++)
    iv[i] = esp_random() & 0xFF;
#elif defined(ESP8266)
  for (int i = 0; i < 16; i++)
    iv[i] = os_random() & 0xFF;
#endif

  // PKCS7 padding
  size_t plainLen = plaintext.length();
  size_t paddedLen = ((plainLen / 16) + 1) * 16;
  uint8_t padValue = paddedLen - plainLen;

  uint8_t *padded = (uint8_t *)malloc(paddedLen);
  if (!padded)
    return "";

  memcpy(padded, plaintext.c_str(), plainLen);
  for (size_t i = plainLen; i < paddedLen; i++) {
    padded[i] = padValue;
  }

  // Encrypt
  uint8_t *encrypted = (uint8_t *)malloc(paddedLen);
  if (!encrypted) {
    free(padded);
    return "";
  }

#if defined(ESP32)
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, _aesKey, 128);

  uint8_t iv_copy[16];
  memcpy(iv_copy, iv, 16);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv_copy, padded,
                        encrypted);
  mbedtls_aes_free(&aes);
#elif defined(ESP8266)
  // ESP8266 BearSSL AES
  br_aes_ct_cbc_keys bc;
  br_aes_ct_cbc_init(&bc, _aesKey, 16);

  uint8_t iv_copy[16];
  memcpy(iv_copy, iv, 16);
  br_aes_ct_cbc_encrypt(&bc, iv_copy, encrypted, padded, paddedLen);
#endif

  free(padded);

  // Format: IV (base64) + ":" + encrypted (base64)
  String result = base64Encode(iv, 16);
  result += ":";
  result += base64Encode(encrypted, paddedLen);

  free(encrypted);
  return result;
}

String AyresWiFiManager::decryptString(const String &ciphertext) {
  if (ciphertext.isEmpty())
    return "";

  // Split IV and encrypted data
  int sepIdx = ciphertext.indexOf(':');
  if (sepIdx < 0)
    return "";

  String ivB64 = ciphertext.substring(0, sepIdx);
  String dataB64 = ciphertext.substring(sepIdx + 1);

  // Decode IV
  uint8_t iv[16];
  size_t ivLen = 16;
  if (!base64Decode(ivB64, iv, &ivLen) || ivLen != 16)
    return "";

  // Decode encrypted data
  size_t encLen = (dataB64.length() * 3 / 4) + 4;
  uint8_t *encrypted = (uint8_t *)malloc(encLen);
  if (!encrypted)
    return "";

  if (!base64Decode(dataB64, encrypted, &encLen)) {
    free(encrypted);
    return "";
  }

  // Decrypt
  uint8_t *decrypted = (uint8_t *)malloc(encLen);
  if (!decrypted) {
    free(encrypted);
    return "";
  }

#if defined(ESP32)
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, _aesKey, 128);

  uint8_t iv_copy[16];
  memcpy(iv_copy, iv, 16);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, encLen, iv_copy, encrypted,
                        decrypted);
  mbedtls_aes_free(&aes);
#elif defined(ESP8266)
  br_aes_ct_cbc_keys bc;
  br_aes_ct_cbc_init(&bc, _aesKey, 16);

  uint8_t iv_copy[16];
  memcpy(iv_copy, iv, 16);
  br_aes_ct_cbc_decrypt(&bc, iv_copy, decrypted, encrypted, encLen);
#endif

  free(encrypted);

  // Remove PKCS7 padding
  uint8_t padValue = decrypted[encLen - 1];
  if (padValue > 0 && padValue <= 16) {
    encLen -= padValue;
  }

  String result = String((char *)decrypted, encLen);
  free(decrypted);
  return result;
}

/* ================================ BEGIN / RUN
 * ================================= */
void AyresWiFiManager::begin() {
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(buttonPin, INPUT_PULLUP);

#if defined(ESP32)
  createTimers(); // Init native timers

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
#else
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif

#if defined(ESP32)
  if (!LittleFS.begin(true)) {
#else
  if (!LittleFS.begin()) {
#endif
    AYLOG_E("❌ Error montando LittleFS");
    return;
  }

  loadCredentials();
}

void AyresWiFiManager::run() {
  // Ventana para detectar hold con feedback LED
  AYLOG_I("🔔 Botón: 2–5s abre portal | ≥5s borra credenciales");

  uint32_t startTime = AWM_now_ms();
  bool pressed = false;

  ledSet(LedPattern::BLINK_SLOW); // guiño durante ventana
  while (AWM_now_ms() - startTime < 2000) {
    if (digitalRead(buttonPin) == LOW) {
      pressed = true;
      break;
    }
    ledTask();
    AWM_sleep_ms(10);
  }
  ledSet(LedPattern::OFF);

  if (pressed) {
    uint32_t t0 = AWM_now_ms();
    while (digitalRead(buttonPin) == LOW) {
      uint32_t held = AWM_now_ms() - t0;
      if (held >= 5000) {
        setLedPatternManual(LedPattern::BLINK_TRIPLE);
        AWM_LOGW("🩹 Hold ≥5s → borrar credenciales y reiniciar");
        eraseCredentials();
        AWM_sleep_ms(900);
        ESP.restart();
      } else if (held >= 2000) {
        setLedPatternManual(LedPattern::BLINK_DOUBLE);
      } else {
        setLedPatternManual(LedPattern::BLINK_FAST);
      }
      ledTask();
      AWM_sleep_ms(10);
    }
    uint32_t held = AWM_now_ms() - t0;
    if (held >= 2000 && held < 5000 && allowButtonPortal) {
      AYLOG_I("🟢 Hold 2–5s → abrir portal");
      setLedAuto(true);
      startPortal();
      return;
    }
    setLedAuto(true);
  }

  // Conectar si hay credenciales
  if (connectToWiFi()) {
    AYLOG_I("✅ Conexión WiFi exitosa.");
    sincronizarHoraNTP();
    ledSet(LedPattern::ON);
    connected = true;
    return;
  }

  // No conectó → actuar según política
  switch (fallbackPolicy) {
  case FallbackPolicy::ON_FAIL:
    AYLOG_I("🟡 Conexión fallida → abriendo portal (policy=ON_FAIL)");
    startPortal();
    break;
  case FallbackPolicy::NO_CREDENTIALS_ONLY:
    if (!tieneCredenciales()) {
      AYLOG_I("🟡 Sin credenciales → abriendo portal");
      startPortal();
    } else {
      AYLOG_I("🟠 Con credenciales → NO abrir portal (NO_CREDENTIALS_ONLY)");
    }
    break;
  case FallbackPolicy::SMART_RETRIES:
    AYLOG_I("🟠 SMART_RETRIES activo → sin portal por ahora; se abrirá si "
            "fallan varios intentos");
    break;
  case FallbackPolicy::BUTTON_ONLY:
    AYLOG_I("🟠 BUTTON_ONLY → no abrir portal automáticamente");
    break;
  case FallbackPolicy::NEVER:
    AYLOG_I("🟠 NEVER → no abrir portal automáticamente");
    break;
  }
}

/* =================================== UPDATE
 * =================================== */
void AyresWiFiManager::update() {
  server.handleClient();
  if (dnsRunning)
    dns.processNextRequest();

  ledAutoUpdate();
  ledTask();

#if defined(ESP32)
  // Manejo de timeout del portal mediante esp_timer
  if (portalActive && portalTimeoutMs && _portalTimeoutExpired) {
    _portalTimeoutExpired = false;
    if (apClientCheck && softAPStationCount() > 0) {
      // Hay clientes conectados → reprogramar timeout
      restartPortalTimeout();
    } else {
      AYLOG_W("⏳ Portal tiempo agotado → cerrando");
      stopPortal();
    }
  }
#else
  // ESP8266: sin FreeRTOS → cálculo clásico por tiempo
  if (portalActive && portalHasTimedOut()) {
    AYLOG_W("⏳ Portal tiempo agotado → cerrando");
    stopPortal();
  }
#endif
}

/* =============================== AP / DNS / HTTP
 * =============================== */
void AyresWiFiManager::redirectToRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void AyresWiFiManager::setupHTTPRoutes() {
  server.on("/", std::bind(&AyresWiFiManager::handleRoot, this));
  server.on("/save", std::bind(&AyresWiFiManager::handleSave, this));
  server.on("/scan", std::bind(&AyresWiFiManager::handleScan, this));
  server.on("/scan.json", std::bind(&AyresWiFiManager::handleScan, this));
  server.on("/erase", HTTP_POST,
            std::bind(&AyresWiFiManager::handleErase, this));

  // NUEVO: info para el portal (versión, host, SSID AP)
  server.on("/info", HTTP_GET, [this]() {
    String hostStr;
#if defined(ESP32)
    hostStr = WiFi.getHostname() ? WiFi.getHostname() : String();
#elif defined(ESP8266)
    hostStr = WiFi.hostname();
#endif
    if (!hostStr.length() && hostname.length())
      hostStr = hostname;

    // IP real del SoftAP con fallback
    String apIp = WiFi.softAPIP().toString();
    if (apIp == "0.0.0.0")
      apIp = apIP.toString();

    String json;
    json.reserve(192);
    json = F("{\"name\":\"AyresWiFiManager\"");
    json += F(",\"version\":\"");
    json += AyresWiFiManager::versionString();
    json += '"';
    json += F(",\"version_code\":");
    json += AyresWiFiManager::versionCode();
    json += F(",\"ap\":\"");
    json += WiFi.softAPSSID();
    json += '"';
    json += F(",\"host\":\"");
    json += hostStr;
    json += '"';
    json += F(",\"ap_ip\":\"");
    json += apIp;
    json += '"';
    json += '}';

    server.send(200, "application/json", json);
  });

  if (captiveEnabled) {
    server.on("/generate_204", [this]() { redirectToRoot(); });
    server.on("/gen_204", [this]() { redirectToRoot(); });
    server.on("/hotspot-detect.html", [this]() { redirectToRoot(); });
    server.on("/connecttest.txt", [this]() { redirectToRoot(); });
    server.on("/ncsi.txt", [this]() { redirectToRoot(); });
    server.on("/fwlink", [this]() { redirectToRoot(); });
  }
  server.on("/favicon.ico", [this]() { server.send(204, "text/plain", ""); });

  server.onNotFound(std::bind(&AyresWiFiManager::handleNotFound, this));
}

void AyresWiFiManager::startDNS() {
  if (dnsRunning)
    return;
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  dnsRunning = true;
}

void AyresWiFiManager::stopDNS() {
  if (!dnsRunning)
    return;
  dns.stop();
  dnsRunning = false;
}

void AyresWiFiManager::setupAP() {
  // FIXED: Usamos AP_STA de entrada para evitar que el cambio de modo
  // en handleScan() desconecte a los clientes.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apGW, apSN);
  WiFi.softAP(apSSID.c_str(), apPASS.c_str());
#if defined(ESP32)
  if (hostname.length())
    WiFi.softAPsetHostname(hostname.c_str());
#endif
  AYLOG_I("📡 AP: %s | IP %s", apSSID.c_str(), apIP.toString().c_str());
}

void AyresWiFiManager::startPortal() {
  if (portalActive)
    return;
  setupAP();
  setupHTTPRoutes();
  server.begin();

  if (captiveEnabled)
    startDNS();
  else if (dnsRunning)
    stopDNS();

  portalActive = true;
  portalStart = AWM_now_ms();
  lastHttpAccess = portalStart;
  AYLOG_I("🌐 Portal cautivo activo en 192.168.4.1 (GET /, /scan, POST /save, "
          "POST /erase, GET /info)");
  ledSet(LedPattern::BLINK_SLOW);

#if defined(ESP32)
  restartPortalTimeout();
#endif
}

void AyresWiFiManager::stopPortal() {
  if (!portalActive)
    return;

// Detener timer de timeout si estaba armado
#if defined(ESP32)
  if (_portalTimer)
    esp_timer_stop(_portalTimer);
  _portalTimeoutExpired = false;
#endif

  stopDNS();
  server.stop();

  if (!externalApActive) {
    WiFi.softAPdisconnect(true);
  } else {
    AYLOG_I("🔒 AP externo activo → preservo SoftAP (no se desconecta).");
  }

  portalActive = false;

  if (externalApActive) {
    WiFi.mode(WIFI_AP);
  } else {
    if (!ssid.isEmpty())
      WiFi.mode(WIFI_STA);
    else
      WiFi.mode(WIFI_OFF);
  }

  AWM_LOGI("✅ Portal cautivo detenido");
}

bool AyresWiFiManager::captivePortalRedirect() {
  if (!portalActive || !captiveEnabled)
    return false;

  String host = server.hostHeader();
  String ap = WiFi.softAPIP().toString();
  if (ap == "0.0.0.0")
    ap = apIP.toString();

  if (host != ap) {
    server.sendHeader("Location", String("http://") + ap, true);
    server.send(302, "text/plain", "");
    server.client().stop();
    return true;
  }
  return false;
}

uint8_t AyresWiFiManager::softAPStationCount() {
  return WiFi.softAPgetStationNum();
}

bool AyresWiFiManager::portalHasTimedOut() {
  if (portalTimeoutMs == 0)
    return false;

  if (apClientCheck && softAPStationCount() > 0) {
    portalStart = AWM_now_ms();
    return false;
  }
  unsigned long base = webClientCheck ? lastHttpAccess : portalStart;
  return (AWM_now_ms() - base) > portalTimeoutMs;
}

/* --------------------------------- HTTP --------------------------------- */
void AyresWiFiManager::handleRoot() {
  if (captivePortalRedirect())
    return;
#if defined(ESP32)
  if (webClientCheck)
    restartPortalTimeout();
#else
  lastHttpAccess = AWM_now_ms();
#endif

  String path = htmlPathPrefix + "index.html";
  if (LittleFS.exists(path)) {
    File file = LittleFS.open(path, "r");
    if (file && !file.isDirectory()) {
      server.send(200, "text/html", file.readString());
      file.close();
      return;
    }
  }

  // Fallback: Embedded GZIP
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html", (const char *)INDEX_HTML_GZ,
                INDEX_HTML_GZ_LEN);
}

void AyresWiFiManager::handleSave() {
  if (captivePortalRedirect())
    return;
#if defined(ESP32)
  if (webClientCheck)
    restartPortalTimeout();
#else
  lastHttpAccess = AWM_now_ms();
#endif

  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Método no permitido");
    return;
  }

  String inSsid = server.arg("ssid");
  String inPass = server.arg("password");
  if (inSsid.isEmpty() || inPass.isEmpty()) {
    mostrarPaginaError("Faltan datos para guardar.");
    return;
  }

  StaticJsonDocument<192> doc;
  doc["ssid"] = inSsid;
  doc["password"] = inPass;

  File file = LittleFS.open("/wifi.json", "w");
  if (!file) {
    mostrarPaginaError("Error al guardar credenciales.");
    return;
  }
  serializeJson(doc, file);
  file.close();

  File success = LittleFS.open(htmlPathPrefix + "success.html", "r");
  if (success) {
    server.send(200, "text/html", success.readString());
    success.close();
  } else {
    // Fallback: Embedded GZIP
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", (const char *)SUCCESS_HTML_GZ,
                  SUCCESS_HTML_GZ_LEN);
  }

  AWM_sleep_ms(1000);
  ESP.restart();
}

void AyresWiFiManager::handleErase() {
  if (captivePortalRedirect())
    return;
#if defined(ESP32)
  if (webClientCheck)
    restartPortalTimeout();
#else
  lastHttpAccess = AWM_now_ms();
#endif

  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Método no permitido");
    return;
  }

  server.send(200, "application/json", "{\"ok\":true}");
  AWM_sleep_ms(150);

  eraseCredentials();

  AWM_sleep_ms(300);
  ESP.restart();
}

void AyresWiFiManager::handleScan() {
#if defined(ESP32)
  if (webClientCheck)
    restartPortalTimeout();
#else
  lastHttpAccess = AWM_now_ms();
#endif

  AYLOG_I("🔍 Escaneando redes WiFi (SYNC, AP+STA)…");

  WiFi.mode(WIFI_AP_STA);
  AWM_sleep_ms(50);

  int st = WiFi.scanComplete();
  if (st == WIFI_SCAN_RUNNING) {
    WiFi.scanDelete();
  }

  // LED: marcar escaneo y mantener parpadeo breve post-scan
  scanning = true;

  // CACHE: Si escaneamos hace muy poco, devolvemos el último resultado
  // Cache de 20 segundos para minimizar escaneos frecuentes
  if (AWM_now_ms() - lastScanAt < 20000 && !lastScanJson.isEmpty()) {
    server.send(200, "application/json", lastScanJson);
    scanning = false;
    AYLOG_I("📋 Usando cache de escaneo");
    return;
  }

  // SCAN RÁPIDO: minimize AP disruption con parámetros optimizados
  AYLOG_I("🔍 Escaneando redes WiFi...");

#if defined(ESP32)
  // Configurar scan rápido en ESP32
  wifi_scan_config_t scanConf;
  scanConf.ssid = nullptr;
  scanConf.bssid = nullptr;
  scanConf.channel = 0; // all channels
  scanConf.show_hidden = false;
  scanConf.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  scanConf.scan_time.active.min = 0;
  scanConf.scan_time.active.max = 120; // 120ms max por canal = scan rápido

  esp_wifi_scan_start(&scanConf, true); // blocking pero rápido
  int n = WiFi.scanComplete();
#else
  // ESP8266: scan estándar
  int n = WiFi.scanNetworks(false, false);
#endif

  if (n < 0) {
    scanning = false;
    server.send(200, "application/json", "[]");
    AYLOG_W("⚠️ Escaneo falló, devolviendo []");
    return;
  }

  size_t cap = 64U + (size_t)n * 64U;
  if (cap < 512U)
    cap = 512U;
  DynamicJsonDocument doc(cap);
  JsonArray arr = doc.to<JsonArray>();

  for (int i = 0; i < n; ++i) {
    String s = WiFi.SSID(i);
    if (!s.length())
      continue;
    JsonObject o = arr.createNestedObject();
    o["ssid"] = s;
    o["rssi"] = WiFi.RSSI(i);
    // Multi-plataforma: ESP32 usa WIFI_AUTH_OPEN, ESP8266 usa AUTH_OPEN
#if defined(ESP32)
    o["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
#elif defined(ESP8266)
    o["secure"] = (WiFi.encryptionType(i) != AUTH_OPEN);
#endif
  }

  WiFi.scanDelete();

  // Mantener blink 1.5 s tras el scan
#if defined(ESP32)
  scanningUntil = 0; // no usamos comparaciones directas

  // Use member esp_timer
  if (_scanTimer) {
    esp_timer_stop(_scanTimer);
    esp_timer_start_once(_scanTimer, 1500ULL * 1000ULL);
  } else {
    scanning = false;
  }
#else
  scanning = false;
  scanningUntil = AWM_now_ms() + 1500;
#endif

  String out;
  serializeJson(arr, out);
  lastScanJson = out; // Update cache
  lastScanAt = AWM_now_ms();
  server.send(200, "application/json", out);
  AYLOG_I("✅ Escaneo OK: %d redes", (int)arr.size());
}

void AyresWiFiManager::handleNotFound() {
  if (captivePortalRedirect())
    return;
#if defined(ESP32)
  if (webClientCheck)
    restartPortalTimeout();
#else
  lastHttpAccess = AWM_now_ms();
#endif
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void AyresWiFiManager::mostrarPaginaError(const String &mensajeFallback) {
  File errorFile = LittleFS.open(htmlPathPrefix + "error.html", "r");
  if (errorFile) {
    server.send(500, "text/html", errorFile.readString());
    errorFile.close();
  } else {
    // Fallback: Embedded GZIP
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(500, "text/html", (const char *)ERROR_HTML_GZ,
                  ERROR_HTML_GZ_LEN);
  }
}

/* ================================= CREDENCIALES
 * ================================ */
bool AyresWiFiManager::tieneCredenciales() const {
  return LittleFS.exists("/wifi.json") && !ssid.isEmpty() &&
         !password.isEmpty();
}

void AyresWiFiManager::loadCredentials() {
  if (!LittleFS.exists("/wifi.json")) {
    AYLOG_I("ℹ️ /wifi.json no existe.");
    return;
  }
  File file = LittleFS.open("/wifi.json", "r");
  if (!file) {
    AYLOG_E("❌ No se pudo abrir /wifi.json");
    return;
  }
  StaticJsonDocument<512> doc; // Increased for encrypted format
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    AWM_LOGE("❌ Error al deserializar JSON de /wifi.json");
    return;
  }

  // Check if encryption is used in the file
  bool fileIsEncrypted = doc["encrypted"].as<bool>();

  String loadedSsid, loadedPassword;

  if (fileIsEncrypted) {
    // Encrypted format
    if (!_encryptionEnabled) {
      AYLOG_E("❌ File is encrypted but encryption is not enabled. Call "
              "enableCredentialEncryption() first.");
      return;
    }

    String encSsid = doc["ssid"].as<String>();
    String encPass = doc["password"].as<String>();

    loadedSsid = decryptString(encSsid);
    loadedPassword = decryptString(encPass);

    if (loadedSsid.isEmpty() || loadedPassword.isEmpty()) {
      AYLOG_E("❌ Decryption failed. Wrong AES key?");
      return;
    }
    AYLOG_I("🔓 Credentials loaded (encrypted)");
  } else {
    // Plain text format (backward compatible)
    loadedSsid = doc["ssid"].as<String>();
    loadedPassword = doc["password"].as<String>();
    AYLOG_I("✅ Credentials loaded (plaintext)");

    // AUTO-MIGRATION: If encryption is enabled but file is plaintext, re-save
    // encrypted
    if (_encryptionEnabled) {
      AYLOG_I("🔒 Migrating plaintext credentials to encrypted format...");
      saveCredentials(loadedSsid, loadedPassword);
    }
  }

  if (loadedSsid.isEmpty() || loadedPassword.isEmpty()) {
    AYLOG_W("⚠️ Credenciales vacías en archivo.");
    return;
  }
  ssid = loadedSsid;
  password = loadedPassword;
  AYLOG_I("✅ SSID=\"%s\" loaded.", ssid.c_str());
}

void AyresWiFiManager::saveCredentials(String s, String p) {
  StaticJsonDocument<512> doc; // Increased for encrypted format

  if (_encryptionEnabled) {
    // Encrypted format
    doc["encrypted"] = true;
    doc["ssid"] = encryptString(s);
    doc["password"] = encryptString(p);
    AYLOG_I("🔒 Saving credentials (encrypted)");
  } else {
    // Plain text format
    doc["encrypted"] = false;
    doc["ssid"] = s;
    doc["password"] = p;
    AYLOG_I("🔓 Saving credentials (plaintext)");
  }

  File file = LittleFS.open("/wifi.json", "w");
  if (!file) {
    AYLOG_E("❌ Error abriendo /wifi.json para escritura");
    return;
  }
  serializeJson(doc, file);
  file.close();
}

void AyresWiFiManager::eraseCredentials() {
  eraseJsonInDir("/");
  AYLOG_I("🧹 Limpieza de .json finalizada (respetando protegidos).");
}

/* ================================= CONEXIÓN STA
 * ================================ */
bool AyresWiFiManager::connectToWiFi() {
  if (!tieneCredenciales())
    return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  AYLOG_I("Conectando a %s", ssid.c_str());

  const uint32_t TOUT_MS = 15000;
  uint32_t t0 = AWM_now_ms();
  while (AWM_now_ms() - t0 < TOUT_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      AYLOG_I("Conectado. IP: %s", WiFi.localIP().toString().c_str());
#if defined(ESP32)
      WiFi.setSleep(false);
#endif
      connected = true;
      return true;
    }
    AWM_sleep_ms(250);
  }

  AYLOG_W("⏱️ Tiempo agotado. No se pudo conectar.");
  connected = false;
  return false;
}

bool AyresWiFiManager::isConnected() {
  connected = (WiFi.status() == WL_CONNECTED);
  return connected;
}

int AyresWiFiManager::getSignalStrength() { return WiFi.RSSI(); }

void AyresWiFiManager::reintentarConexionSiNecesario() {
  if (!autoReconnect)
    return;
  if (WiFi.status() == WL_CONNECTED) {
    connected = true;
    return;
  }

  connected = false;
  uint32_t ahora = AWM_now_ms();

  if (ahora - ultimoIntentoWiFi < reconnectBackoffMs)
    return;
  ultimoIntentoWiFi = ahora;

  if (!ssid.isEmpty() && !password.isEmpty()) {
    AYLOG_I("🔁 Intentando reconexión WiFi... (ventana=%lu ms, backoff=%lu ms)",
            (unsigned long)reconnectAttemptMs,
            (unsigned long)reconnectBackoffMs);

    if (portalActive || externalApActive)
      WiFi.mode(WIFI_AP_STA);
    else
      WiFi.mode(WIFI_STA);

    WiFi.begin(ssid.c_str(), password.c_str());
    uint32_t t0 = AWM_now_ms();
    bool ok = false;

    while (AWM_now_ms() - t0 < reconnectAttemptMs) {
      if (WiFi.status() == WL_CONNECTED) {
        ok = true;
        break;
      }
      AWM_sleep_ms(250);
    }
    if (ok) {
      AYLOG_I("🔌 Reconectado a WiFi.");
      sincronizarHoraNTP();
      connected = true;
      failCount = 0;
      failWindowStart = 0;
      return;
    }
    AYLOG_W("❌ Reconexión WiFi fallida.");

    if (fallbackPolicy == FallbackPolicy::SMART_RETRIES) {
      if (failWindowStart == 0 ||
          (AWM_now_ms() - failWindowStart) > failWindowMs) {
        failWindowStart = AWM_now_ms();
        failCount = 0;
      }
      failCount++;
      AYLOG_D("📉 SMART: fallos=%u/%u en %lu ms", failCount, maxFailRetries,
              (unsigned long)(AWM_now_ms() - failWindowStart));
      if (failCount >= maxFailRetries) {
        AYLOG_W("🚪 SMART: abriendo portal por fallos acumulados");
        startPortal();
        failCount = 0;
        failWindowStart = 0;
      }
    }
  }
}

bool AyresWiFiManager::scanRedDetectada() {
  uint32_t ahora = AWM_now_ms();
  if (ahora - ultimoScan < SCAN_INTERVAL_MS)
    return false;
  ultimoScan = ahora;

  if (WiFi.status() == WL_CONNECTED && !portalActive)
    return false;

  int n = WiFi.scanNetworks(false, false);
  bool encontrada = false;
  for (int i = 0; i < n; ++i) {
    if (WiFi.SSID(i) == ssid) {
      encontrada = true;
      break;
    }
  }
  WiFi.scanDelete();
  return encontrada;
}

void AyresWiFiManager::forzarReconexion() {
  AYLOG_I("🔄  Forzando reconexión…");
  if (portalActive || externalApActive)
    WiFi.mode(WIFI_AP_STA);
  else
    WiFi.mode(WIFI_STA);

  WiFi.begin(ssid.c_str(), password.c_str());
  ultimoIntentoWiFi = AWM_now_ms();
}

/* =================================== NTP / TIEMPO
 * =================================== */
void AyresWiFiManager::sincronizarHoraNTP() {
  if (WiFi.status() != WL_CONNECTED)
    return;

  // ⚙️ Timezone:
  //  - "UTC0"  → UTC en logs (default)
  //  - "<-03>3"→ Hora Argentina (sin DST)
#if defined(ESP32)
  const char *tz = "UTC0";
  configTzTime(tz, "time.google.com", "time.cloudflare.com", "pool.ntp.org");
#else
  configTime(0, 0, "time.google.com", "time.cloudflare.com", "pool.ntp.org");
#endif
  AYLOG_I("📡 Sincronizando hora (NTP)…");

  struct tm ti{};
  const uint32_t perTryMs = 10000; // 10 s por intento
  const int maxTries = 3;          // total ~30 s

  for (int i = 0; i < maxTries; ++i) {
    if (AWM_waitLocalTime_(&ti, perTryMs)) {
      char buf[32];
      strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", &ti);
      AYLOG_I("🕒 Hora sincronizada: %s", buf);
      // (Quitado) sntp_set_sync_interval(...) no está disponible en todos los
      // cores.
      return;
    }
    AYLOG_W("⏳ NTP intento %d/%d sin respuesta; reintentando…", i + 1,
            maxTries);

    // Rotamos servidores por si alguno está lento/bloqueado
#if defined(ESP32)
    switch (i) {
    case 0:
      configTzTime(tz, "pool.ntp.org", "time.nist.gov", "time.google.com");
      break;
    case 1:
      configTzTime(tz, "time.cloudflare.com", "pool.ntp.org", "time.nist.gov");
      break;
    default:
      break;
    }
#else
    switch (i) {
    case 0:
      configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
      break;
    case 1:
      configTime(0, 0, "time.cloudflare.com", "pool.ntp.org", "time.nist.gov");
      break;
    default:
      break;
    }
#endif
  }

#if defined(ESP32)
  // Fallback “sí o sí” por HTTP Date si NTP no responde (solo ESP32)
  AYLOG_W("🌐 NTP lento/bloqueado; intento fallback por HTTP Date…");
  if (AWM_syncTimeFromHttp_("http://google.com", 6000) ||
      AWM_syncTimeFromHttp_("http://worldtimeapi.org/api/ip", 8000)) {
    return;
  }
#endif

  AYLOG_W("⚠️ No pude sincronizar hora (NTP/HTTP). Reintentaré luego.");
}

uint64_t AyresWiFiManager::getTimestamp() {
  time_t now = time(nullptr);
  return (now > 100000) ? static_cast<uint64_t>(now) * 1000ULL : 0;
}

/* =============================== INTERNET CHECK
 * =============================== */
bool AyresWiFiManager::hayInternet() {
  if (WiFi.status() != WL_CONNECTED)
    return false;
  WiFiClient client;
  HTTPClient http;
  http.begin(client, "http://clients3.google.com/generate_204");
#if defined(ESP32)
  http.setConnectTimeout(3000);
#else
  http.setTimeout(3000);
#endif
  int httpCode = http.GET();
  http.end();
  return (httpCode == 204);
}

/* =================================== LED FSM
 * =================================== */
void AyresWiFiManager::setLedAuto(bool enable) {
  ledAuto = enable;
  if (ledAuto)
    ledSet(LedPattern::OFF);
}
void AyresWiFiManager::setLedPatternManual(LedPattern p) {
  ledAuto = false;
  ledSet(p);
}
void AyresWiFiManager::ledSet(LedPattern p) {
  ledPat = p;
  ledStep = 0;
  ledT0 = AWM_now_ms();
}
void AyresWiFiManager::ledAutoUpdate() {
  if (!ledAuto)
    return;

  LedPattern want = LedPattern::OFF;

  // prioridad: escaneo > portal > conectado > idle
#if defined(ESP32)
  if (scanning)
    want = LedPattern::BLINK_FAST;
#else
  if (scanning || (AWM_now_ms() < scanningUntil))
    want = LedPattern::BLINK_FAST;
#endif
  else if (portalActive)
    want = LedPattern::BLINK_SLOW;
  else if (WiFi.status() == WL_CONNECTED)
    want = LedPattern::ON;
  else
    want = LedPattern::OFF;

  if (want != ledPat)
    ledSet(want);
}

void AyresWiFiManager::ledTask() {
  const uint32_t now = AWM_now_ms();

  auto write = [&](uint8_t v) {
    if (ledOut != v) {
      ledOut = v;
      digitalWrite(ledPin, v);
    }
  };

  switch (ledPat) {
  case LedPattern::OFF:
    write(LOW);
    break;
  case LedPattern::ON:
    write(HIGH);
    break;

  case LedPattern::BLINK_SLOW: { // 500ms ON / 500ms OFF
    const uint32_t period = 1000;
    write(((now - ledT0) % period) < 500 ? HIGH : LOW);
  } break;

  case LedPattern::BLINK_FAST: { // 100ms ON / 100ms OFF
    const uint32_t period = 200;
    write(((now - ledT0) % period) < 100 ? HIGH : LOW);
  } break;

  case LedPattern::BLINK_DOUBLE: { // ON 120, OFF 120, ON 120, OFF 640
    static const uint16_t seq[] = {120, 120, 120, 640};
    static const uint8_t on[] = {1, 0, 1, 0};
    if (now - ledT0 >= seq[ledStep]) {
      ledT0 = now;
      ledStep = (ledStep + 1) % 4;
      write(on[ledStep] ? HIGH : LOW);
    }
  } break;

  case LedPattern::BLINK_TRIPLE: { // ON 100, OFF 100 x3, OFF 500
    static const uint16_t seq[] = {100, 100, 100, 100, 100, 500};
    static const uint8_t on[] = {1, 0, 1, 0, 1, 0};
    if (now - ledT0 >= seq[ledStep]) {
      ledT0 = now;
      ledStep = (ledStep + 1) % 6;
      write(on[ledStep] ? HIGH : LOW);
    }
  } break;
  }
}

/* =============================== RECONNECT DRIVER
 * =============================== */
void AyresWiFiManager::setAutoReconnect(bool habilitado) {
  autoReconnect = habilitado;
  WiFi.setAutoReconnect(habilitado);
}

/* ======================= Helpers estáticos (borrado de JSONs)
 * ======================= */
void AyresWiFiManager::setProtectedJsons(
    std::initializer_list<const char *> names) {
  _protectedExact.clear();
  for (auto n : names) {
    String s(n ? n : "");
    if (!s.length())
      continue;
    if (!s.startsWith("/"))
      s = "/" + s;
    _protectedExact.push_back(s);
  }
}

bool AyresWiFiManager::isProtectedJson(const String &name) const {
  String n = name;
  if (!n.startsWith("/"))
    n = "/" + n;

  for (const auto &ex : _protectedExact) {
    if (n.equalsIgnoreCase(ex))
      return true;
  }
  return false;
}

#if defined(ESP32)
// Recursivo + cierra el File ANTES de borrar (evita "Has open FD")
void AyresWiFiManager::eraseJsonInDir(const char *dirPath) {
  if (!dirPath || !*dirPath)
    return;

  File dir = LittleFS.open(dirPath);
  if (!dir || !dir.isDirectory())
    return;

  String base = dirPath;
  if (!base.endsWith("/"))
    base += "/";

  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    String full = name;
    if (!full.startsWith("/"))
      full = base + full;

    const bool isDir = f.isDirectory();
    f.close();

    if (isDir) {
      eraseJsonInDir(full.c_str());
    } else {
      if (full.endsWith(".json") && !isProtectedJson(full)) {
        if (LittleFS.remove(full)) {
          AWM_LOGI("🗑️  Borrado: %s", full.c_str());
        } else {
          AWM_LOGW("⚠️  No se pudo borrar: %s", full.c_str());
        }
      }
    }
  }
  dir.close();
}
#else // ESP8266 (no recursivo)
void AyresWiFiManager::eraseJsonInDir(const char *dirPath) {
  if (!dirPath || !*dirPath)
    return;

  Dir d = LittleFS.openDir(dirPath);
  while (d.next()) {
    String name = d.fileName();
    String full = name;
    if (!full.startsWith("/"))
      full = String("/") + full;

    if (full.endsWith(".json") && !isProtectedJson(full)) {
      if (LittleFS.remove(full)) {
        AYLOG_I("🗑️  Borrado: %s", full.c_str());
      } else {
        AYLOG_W("⚠️  No se pudo borrar: %s", full.c_str());
      }
    }
  }
}
#endif

/* ====================== Portal timeout por esp_timer (ESP32)
 * ====================== */
#if defined(ESP32)
void AyresWiFiManager::restartPortalTimeout() {
  if (!portalActive || portalTimeoutMs == 0)
    return;
  _portalTimeoutExpired = false;

  const uint64_t us = (uint64_t)portalTimeoutMs * 1000ULL;

  if (_portalTimer) {
    esp_timer_stop(_portalTimer);
    esp_timer_start_once(_portalTimer, us);
  }
}

void AyresWiFiManager::createTimers() {
  esp_timer_create_args_t portalArgs = {};
  portalArgs.callback = &_onPortalTimerCallback;
  portalArgs.arg = this;
  portalArgs.name = "AWM_Portal";
  esp_timer_create(&portalArgs, &_portalTimer);

  esp_timer_create_args_t scanArgs = {};
  scanArgs.callback = &_onScanTimerCallback;
  scanArgs.arg = this;
  scanArgs.name = "AWM_Scan";
  esp_timer_create(&scanArgs, &_scanTimer);
}

void IRAM_ATTR AyresWiFiManager::_onPortalTimerCallback(void *arg) {
  if (arg)
    reinterpret_cast<AyresWiFiManager *>(arg)->_portalTimeoutExpired = true;
}

void IRAM_ATTR AyresWiFiManager::_onScanTimerCallback(void *arg) {
  if (arg)
    reinterpret_cast<AyresWiFiManager *>(arg)->scanning = false;
}
#endif
