/*
 *  AyresWiFiManager — Firmware Library
 *  =========================================================================
 *  Archivo   : AyresWiFiManager.cpp
 *  Versión   : 2.3.0
 *  Autor     : Daniel C. Salgado
 *  Empresa   : AyresNet IoT Systems
 *  Repositorio: https://github.com/ayresnet/AyresWiFiManager
 *
 *  © 2025 AyresNet IoT Systems. Todos los derechos reservados.
 *  Licencia  : MIT License
 *
 *  DESCRIPCIÓN
 *  =========================================================================
 *  Gestor de conectividad Wi-Fi y provisión para ESP32.
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
 *  - Estado unificado y diagnóstico de conectividad
 *
 *  CHANGELOG (Semantic Versioning)
 *  =========================================================================
 *  v2.3.0 (2026-08-07)
 *    + Estado unificado de conectividad y diagnóstico público
 *    + Logging y documentación preparados para uso como librería
 *    + Soporte oficial concentrado en ESP32
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
 *  ESP32: FreeRTOS timing, esp_timer, auto-format LittleFS, WiFi PS_NONE
 *
 *  DEPENDENCIAS
 *  =========================================================================
 *  - ArduinoJson >= 6.21.2
 *  - LittleFS (incluido en framework)
 *  - DNSServer (incluido en framework)
 *  - WebServer (incluido en framework ESP32)
 *
 *  DOCUMENTACIÓN COMPLETA
 *  =========================================================================
 *  Ver archivo AyresWiFiManager.h para API completa y ejemplos de uso.
 */

#include "AyresWiFiManager.h"
#include "AWM_html_gz.h"
#include "AyresLog.h"
#include <ArduinoJson.h>

// Macros directas de AyresLog son usadas en el código, no hace falta mapeo

#include <HTTPClient.h>
#include <esp_mac.h>
#include <esp_task_wdt.h> // Watchdog de hardware
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/time.h> // settimeofday (fallback HTTP Date)

// mbedTLS for AES encryption
#include "esp_system.h" // for esp_random()
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "mbedtls/gcm.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ===== Helpers de tiempo ===== */
static inline uint32_t AWM_now_ms() {
  return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
static inline void AWM_sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

String AyresWiFiManager::getMacAddress() {
  uint8_t mac[6]{};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
    return String();

  char address[18]{};
  snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(address);
}

String AyresWiFiManager::getMacSuffix() {
  uint8_t mac[6]{};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
    return String();

  char suffix[5]{};
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  return String(suffix);
}

/* ============================== Helpers NTP/Fecha
 * =============================== */

// Espera a que el tiempo esté disponible en ESP32.
static bool AWM_waitLocalTime_(struct tm *ti, uint32_t timeoutMs) {
  return getLocalTime(ti, timeoutMs);
}

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

bool AyresWiFiManager::setAPCredentialsUsingStoredPassword(
    const String &ssid_) {
  if (ssid_.isEmpty() || password.length() < 8 || password.length() > 63)
    return false;
  apSSID = ssid_;
  apPASS = password;
  return true;
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

/* ============================= CREDENTIAL ENCRYPTION (AES-128-GCM)
 * ================================ */
bool AyresWiFiManager::setCredentialEncryption(bool enabled,
                                               const char *aes_key) {
  if (!enabled) {
    _encryptionEnabled = false;
    if (aes_key) {
      if (strlen(aes_key) != sizeof(_aesKey)) {
        _aesKeyConfigured = false;
        memset(_aesKey, 0, sizeof(_aesKey));
        _lastError = Error::ENCRYPTION_ERROR;
        AYLOG_E("La clave de migración debe tener exactamente 16 bytes");
        return false;
      }
      memcpy(_aesKey, aes_key, sizeof(_aesKey));
      _aesKeyConfigured = true;
    } else {
      _aesKeyConfigured = false;
      memset(_aesKey, 0, sizeof(_aesKey));
    }
    _lastError = Error::NONE;
    AYLOG_I("Almacenamiento de credenciales en texto plano");
    return true;
  }

  if (!aes_key || strlen(aes_key) != sizeof(_aesKey)) {
    _encryptionEnabled = false;
    _aesKeyConfigured = false;
    memset(_aesKey, 0, sizeof(_aesKey));
    _lastError = Error::ENCRYPTION_ERROR;
    AYLOG_E("La clave de cifrado debe tener exactamente 16 bytes");
    return false;
  }

  memcpy(_aesKey, aes_key, sizeof(_aesKey));
  _encryptionEnabled = true;
  _aesKeyConfigured = true;
  _lastError = Error::NONE;
  AYLOG_I("Almacenamiento cifrado de credenciales habilitado");
  return true;
}

void AyresWiFiManager::enableCredentialEncryption(const char *aes_key) {
  setCredentialEncryption(true, aes_key);
}

void AyresWiFiManager::disableCredentialEncryption() {
  setCredentialEncryption(false);
}

bool AyresWiFiManager::isEncryptionEnabled() const {
  return _encryptionEnabled;
}

String AyresWiFiManager::encryptCredentialEnvelope(const String &networkName,
                                                    const String &networkPass) {
  static const uint8_t magic[] = {'A', 'W', 'M', 3};
  static const uint8_t aad[] = "AWM-CREDENTIAL-V3";
  constexpr size_t MAGIC_LEN = sizeof(magic);
  constexpr size_t NONCE_LEN = 12;
  constexpr size_t TAG_LEN = 16;

  StaticJsonDocument<320> credentials;
  credentials["s"] = networkName;
  credentials["p"] = networkPass;
  if (credentials.overflowed())
    return "";
  String plaintext;
  if (serializeJson(credentials, plaintext) == 0)
    return "";

  const size_t plainLen = plaintext.length();
  const size_t packetLen = MAGIC_LEN + NONCE_LEN + plainLen + TAG_LEN;
  uint8_t *packet = static_cast<uint8_t *>(malloc(packetLen));
  if (!packet)
    return "";

  memcpy(packet, magic, MAGIC_LEN);
  uint8_t *nonce = packet + MAGIC_LEN;
  uint8_t *encrypted = nonce + NONCE_LEN;
  uint8_t *tag = encrypted + plainLen;
  for (size_t i = 0; i < NONCE_LEN; ++i)
    nonce[i] = static_cast<uint8_t>(esp_random());

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, _aesKey, 128);
  if (rc == 0) {
    rc = mbedtls_gcm_crypt_and_tag(
        &gcm, MBEDTLS_GCM_ENCRYPT, plainLen, nonce, NONCE_LEN, aad,
        sizeof(aad) - 1, reinterpret_cast<const uint8_t *>(plaintext.c_str()),
        encrypted, TAG_LEN, tag);
  }
  mbedtls_gcm_free(&gcm);

  String envelope;
  if (rc == 0)
    envelope = base64Encode(packet, packetLen);
  memset(packet, 0, packetLen);
  free(packet);
  return envelope;
}

bool AyresWiFiManager::decryptCredentialEnvelope(const String &envelope,
                                                 String &networkName,
                                                 String &networkPass) {
  static const uint8_t magic[] = {'A', 'W', 'M', 3};
  static const uint8_t aad[] = "AWM-CREDENTIAL-V3";
  constexpr size_t MAGIC_LEN = sizeof(magic);
  constexpr size_t NONCE_LEN = 12;
  constexpr size_t TAG_LEN = 16;
  constexpr size_t OVERHEAD = MAGIC_LEN + NONCE_LEN + TAG_LEN;

  size_t packetLen = (envelope.length() * 3 / 4) + 4;
  uint8_t *packet = static_cast<uint8_t *>(malloc(packetLen));
  if (!packet)
    return false;
  if (!base64Decode(envelope, packet, &packetLen) || packetLen <= OVERHEAD ||
      memcmp(packet, magic, MAGIC_LEN) != 0) {
    memset(packet, 0, packetLen);
    free(packet);
    return false;
  }

  const size_t encryptedLen = packetLen - OVERHEAD;
  const uint8_t *nonce = packet + MAGIC_LEN;
  const uint8_t *encrypted = nonce + NONCE_LEN;
  const uint8_t *tag = encrypted + encryptedLen;
  uint8_t *decrypted = static_cast<uint8_t *>(malloc(encryptedLen + 1));
  if (!decrypted) {
    memset(packet, 0, packetLen);
    free(packet);
    return false;
  }

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, _aesKey, 128);
  if (rc == 0) {
    rc = mbedtls_gcm_auth_decrypt(
        &gcm, encryptedLen, nonce, NONCE_LEN, aad, sizeof(aad) - 1, tag,
        TAG_LEN, encrypted, decrypted);
  }
  mbedtls_gcm_free(&gcm);
  memset(packet, 0, packetLen);
  free(packet);

  if (rc != 0) {
    memset(decrypted, 0, encryptedLen + 1);
    free(decrypted);
    return false;
  }
  decrypted[encryptedLen] = '\0';

  StaticJsonDocument<320> credentials;
  DeserializationError jsonError = deserializeJson(
      credentials, reinterpret_cast<char *>(decrypted), encryptedLen);
  const char *decodedName = credentials["s"];
  const char *decodedPass = credentials["p"];
  const bool valid = !jsonError && decodedName && decodedName[0] && decodedPass;
  if (valid) {
    networkName = decodedName;
    networkPass = decodedPass;
  }
  memset(decrypted, 0, encryptedLen + 1);
  free(decrypted);
  return valid;
}

String AyresWiFiManager::base64Encode(const uint8_t *data, size_t len) {
  size_t olen = 0;
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
  return "";
}

bool AyresWiFiManager::base64Decode(const String &b64, uint8_t *out,
                                    size_t *outLen) {
  return (mbedtls_base64_decode(out, *outLen, outLen,
                                (const uint8_t *)b64.c_str(),
                                b64.length()) == 0);
}

String AyresWiFiManager::decryptString(const String &ciphertext) {
  if (ciphertext.isEmpty())
    return "";

  if (ciphertext.startsWith("gcm:")) {
    static const uint8_t aad[] = "AWM-CREDENTIAL-V2";
    constexpr size_t NONCE_LEN = 12;
    constexpr size_t TAG_LEN = 16;

    const int nonceEnd = ciphertext.indexOf(':', 4);
    const int dataEnd = ciphertext.indexOf(':', nonceEnd + 1);
    if (nonceEnd < 0 || dataEnd < 0)
      return "";

    const String nonceB64 = ciphertext.substring(4, nonceEnd);
    const String dataB64 = ciphertext.substring(nonceEnd + 1, dataEnd);
    const String tagB64 = ciphertext.substring(dataEnd + 1);

    uint8_t nonce[NONCE_LEN];
    size_t nonceLen = sizeof(nonce);
    uint8_t tag[TAG_LEN];
    size_t tagLen = sizeof(tag);
    if (!base64Decode(nonceB64, nonce, &nonceLen) || nonceLen != NONCE_LEN ||
        !base64Decode(tagB64, tag, &tagLen) || tagLen != TAG_LEN)
      return "";

    size_t encryptedLen = (dataB64.length() * 3 / 4) + 4;
    uint8_t *encrypted = static_cast<uint8_t *>(malloc(encryptedLen));
    if (!encrypted)
      return "";
    if (!base64Decode(dataB64, encrypted, &encryptedLen) ||
        encryptedLen == 0) {
      free(encrypted);
      return "";
    }

    uint8_t *decrypted = static_cast<uint8_t *>(malloc(encryptedLen));
    if (!decrypted) {
      free(encrypted);
      return "";
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, _aesKey, 128);
    if (rc == 0) {
      rc = mbedtls_gcm_auth_decrypt(
          &gcm, encryptedLen, nonce, NONCE_LEN, aad, sizeof(aad) - 1, tag,
          TAG_LEN, encrypted, decrypted);
    }
    mbedtls_gcm_free(&gcm);
    memset(encrypted, 0, encryptedLen);
    free(encrypted);

    if (rc != 0) {
      memset(decrypted, 0, encryptedLen);
      free(decrypted);
      return "";
    }

    String result(reinterpret_cast<char *>(decrypted), encryptedLen);
    memset(decrypted, 0, encryptedLen);
    free(decrypted);
    return result;
  }

  // Compatibilidad de lectura con AES-128-CBC de AWM <= 2.2.1.
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
  if (encLen == 0 || (encLen % 16) != 0) {
    free(encrypted);
    return "";
  }

  // Decrypt
  uint8_t *decrypted = (uint8_t *)malloc(encLen);
  if (!decrypted) {
    free(encrypted);
    return "";
  }

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  int aesRc = mbedtls_aes_setkey_dec(&aes, _aesKey, 128);

  uint8_t iv_copy[16];
  memcpy(iv_copy, iv, 16);
  if (aesRc == 0) {
    aesRc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, encLen, iv_copy,
                                  encrypted, decrypted);
  }
  mbedtls_aes_free(&aes);

  memset(encrypted, 0, encLen);
  free(encrypted);

  if (aesRc != 0) {
    memset(decrypted, 0, encLen);
    free(decrypted);
    return "";
  }

  // Validar y remover PKCS#7 del formato anterior.
  uint8_t padValue = decrypted[encLen - 1];
  if (padValue == 0 || padValue > 16 || padValue > encLen) {
    memset(decrypted, 0, encLen);
    free(decrypted);
    return "";
  }
  for (size_t i = encLen - padValue; i < encLen; ++i) {
    if (decrypted[i] != padValue) {
      memset(decrypted, 0, encLen);
      free(decrypted);
      return "";
    }
  }
  const size_t paddedLen = encLen;
  encLen -= padValue;

  String result = String((char *)decrypted, encLen);
  memset(decrypted, 0, paddedLen);
  free(decrypted);
  return result;
}

bool AyresWiFiManager::verifyWiFiPassword(const String &candidate) const {
  if (password.isEmpty())
    return false;

  size_t difference = candidate.length() ^ password.length();
  for (size_t i = 0; i < 64; ++i) {
    const uint8_t expected =
        i < password.length() ? static_cast<uint8_t>(password[i]) : 0;
    const uint8_t received =
        i < candidate.length() ? static_cast<uint8_t>(candidate[i]) : 0;
    difference |= expected ^ received;
  }
  return difference == 0;
}

String AyresWiFiManager::getWiFiPass() const { return password; }

/* ================================ BEGIN / RUN
 * ================================= */
void AyresWiFiManager::begin() {
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(buttonPin, INPUT_PULLUP);

  createTimers(); // Init native timers

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  if (!LittleFS.begin(true)) {
    _lastError = Error::STORAGE_ERROR;
    AYLOG_E("❌ Error montando LittleFS");
    return;
  }

  loadCredentials();
}

void AyresWiFiManager::run() {
  // Ventana para detectar hold con feedback LED
  AYLOG_I("Botón: 2-5 s abre portal | 5 s o más borra credenciales");

  uint32_t startTime = AWM_now_ms();
  bool pressed = false;

  ledSet(LedPattern::BLINK_SLOW); // guiño durante ventana
  // Ampliar ventana de detección inicial a 3s
  while (AWM_now_ms() - startTime < 3000) {
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
        AWM_LOGW("Pulsación de 5 s o más: borrar credenciales y reiniciar");
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
      AYLOG_I("Pulsación de 2-5 s: abrir portal");
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
  esp_task_wdt_reset(); // FEED DOG in inner loop
  server.handleClient();
  if (dnsRunning)
    dns.processNextRequest();

  ledAutoUpdate();
  ledTask();

  const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  connected = wifiConnected;
  if (!wifiConnected && _state != State::WIFI_CONNECTING)
    _state = State::OFFLINE;
  else if (wifiConnected &&
           (_state == State::OFFLINE || _state == State::WIFI_CONNECTING))
    _state = State::WIFI_CONNECTED;

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
    hostStr = WiFi.getHostname() ? WiFi.getHostname() : String();
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
  if (WiFi.getMode() != WIFI_AP_STA) {
    WiFi.mode(WIFI_AP_STA);
    AWM_sleep_ms(100); // Give time for hardware to settle
  }

  WiFi.softAPConfig(apIP, apGW, apSN);
  WiFi.softAP(apSSID.c_str(), apPASS.c_str());
  if (hostname.length())
    WiFi.softAPsetHostname(hostname.c_str());
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

  // CRITICAL: Reinit WDT before starting portal (WiFi mode changes can reset
  // it)
#if defined(ESP_ARDUINO_VERSION) &&                                             \
    ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  // ESP-IDF v5 / Arduino-ESP32 core 3.x: esp_task_wdt_init() takes a config
  // struct, and the Task WDT is already started at boot, so reconfigure it
  // instead of re-initialising.
  esp_task_wdt_config_t wdt_cfg = {
      .timeout_ms = 120000,
      .idle_core_mask = 0,
      .trigger_panic = true,
  };
  if (esp_task_wdt_init(&wdt_cfg) == ESP_ERR_INVALID_STATE)
    esp_task_wdt_reconfigure(&wdt_cfg);
#else
  esp_task_wdt_init(120, true);
#endif
  esp_task_wdt_add(NULL);
  AYLOG_I("✅ WDT re-inicializado en startPortal (120s)");

  AYLOG_I("🌐 Portal cautivo activo en 192.168.4.1 (GET /, /scan, POST /save, "
          "POST /erase, GET /info)");
  ledSet(LedPattern::BLINK_SLOW);

  restartPortalTimeout();
}

void AyresWiFiManager::stopPortal() {
  if (!portalActive)
    return;

// Detener timer de timeout si estaba armado
  if (_portalTimer)
    esp_timer_stop(_portalTimer);
  _portalTimeoutExpired = false;

  stopDNS();
  server.stop();

  if (!externalApActive) {
    WiFi.softAPdisconnect(true);
  } else {
    AYLOG_I("🔒 AP externo activo → preservo SoftAP (no se desconecta).");
  }

  portalActive = false;

  _state = (WiFi.status() == WL_CONNECTED) ? State::WIFI_CONNECTED
                                            : State::OFFLINE;

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

/* --------------------------------- HTTP --------------------------------- */
void AyresWiFiManager::handleRoot() {
  esp_task_wdt_reset(); // Feed WDT in HTTP handler

  if (captivePortalRedirect())
    return;
  if (webClientCheck)
    restartPortalTimeout();

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
  if (webClientCheck)
    restartPortalTimeout();

  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Método no permitido");
    return;
  }

  String inSsid = server.arg("ssid");
  String inPass = server.arg("password");
  if (inSsid.isEmpty()) {
    mostrarPaginaError("El nombre de la red Wi-Fi es obligatorio.");
    return;
  }

  if (!saveCredentials(inSsid, inPass)) {
    mostrarPaginaError("Error al guardar credenciales.");
    return;
  }

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
  if (webClientCheck)
    restartPortalTimeout();

  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Método no permitido");
    return;
  }

  const String scope = server.arg("scope");
  const String confirmation = server.arg("confirm");
  EraseResult result;

  if (scope == "wifi") {
    if (confirmation != "WIFI_ONLY") {
      server.send(400, "application/json",
                  "{\"ok\":false,\"error\":\"confirmation_required\"}");
      return;
    }

    result.found = LittleFS.exists("/wifi.json") ? 1 : 0;
    const bool ok = eraseWiFiCredentials();
    result.removed = (result.found && ok) ? 1 : 0;
    result.failed = ok ? 0 : 1;
  } else if (scope == "all") {
    if (confirmation != "DELETE_ALL_JSON") {
      server.send(400, "application/json",
                  "{\"ok\":false,\"error\":\"confirmation_required\"}");
      return;
    }

    const bool forceAll = server.arg("force") == "1";
    // Con force=1 incluye wifi.json y no aplica la lista de protegidos. Los
    // handles propios se cierran antes de comenzar la fase de borrado.
    eraseJsonInDir("/", !forceAll, result);
  } else {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"invalid_scope\"}");
    return;
  }

  StaticJsonDocument<192> response;
  response["ok"] = (result.failed == 0);
  response["scope"] = scope;
  response["found"] = result.found;
  response["removed"] = result.removed;
  response["failed"] = result.failed;
  response["restarting"] = (result.failed == 0);
  String json;
  serializeJson(response, json);
  server.send(result.failed == 0 ? 200 : 500, "application/json", json);

  if (result.failed == 0) {
    AWM_sleep_ms(800);
    ESP.restart();
  }
}

void AyresWiFiManager::handleScan() {
  esp_task_wdt_reset(); // Feed WDT before scan
  if (webClientCheck)
    restartPortalTimeout();

  AYLOG_I("🔍 Escaneando redes WiFi (ASYNC, AP+STA)…");

  WiFi.mode(WIFI_AP_STA);
  AWM_sleep_ms(50);

  int st = WiFi.scanComplete();

  // Si hay scan en curso, informar que está en progreso
  if (st == WIFI_SCAN_RUNNING) {
    server.send(202, "application/json", "{\"scanning\":true}");
    AYLOG_I("⏳ Escaneo en progreso…");
    return;
  }

  // LED: marcar escaneo
  scanning = true;

  // CACHE: Si escaneamos hace muy poco, devolvemos el último resultado
  if (AWM_now_ms() - lastScanAt < 20000 && !lastScanJson.isEmpty()) {
    server.send(200, "application/json", lastScanJson);
    scanning = false;
    AYLOG_I("📋 Usando cache de escaneo");
    return;
  }

  // Si hay resultados viejos, limpiarlos
  if (st >= 0) {
    WiFi.scanDelete();
  }

  // SYNC SCAN RÁPIDO: Con WDT de 120s podemos bloquear ~8s sin problema
  AYLOG_I("🔍 Escaneando redes WiFi...");

  // Configurar scan rápido en ESP32
  wifi_scan_config_t scanConf;
  scanConf.ssid = nullptr;
  scanConf.bssid = nullptr;
  scanConf.channel = 0; // all channels
  scanConf.show_hidden = false;
  scanConf.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  scanConf.scan_time.active.min = 0;
  scanConf.scan_time.active.max = 120; // 120ms max por canal

  esp_wifi_scan_start(&scanConf, true); // true = blocking (sync)
  int n = WiFi.scanComplete();

  if (n < 0) {
    scanning = false;
    server.send(200, "application/json", "[]");
    AYLOG_W("⚠️ Escaneo falló, devolviendo []");
    return;
  }

  AYLOG_I("✅ Escaneo OK: %d redes", n);

  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.to<JsonArray>();

  for (int i = 0; i < n; ++i) {
    esp_task_wdt_reset(); // Feed WDT durante procesamiento
    JsonObject obj = arr.createNestedObject();
    obj["ssid"] = WiFi.SSID(i);
    obj["rssi"] = WiFi.RSSI(i);
    const bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    obj["encryption"] = secure ? 1 : 0;
    obj["secure"] = secure;
  }

  // Guardar en cache
  lastScanJson = "";
  serializeJson(arr, lastScanJson);
  lastScanAt = AWM_now_ms();

  // Borrar resultados
  WiFi.scanDelete();

  String out;
  serializeJson(arr, out);
  server.send(200, "application/json", out);
  scanning = false;
}

void AyresWiFiManager::handleNotFound() {
  if (captivePortalRedirect())
    return;
  if (webClientCheck)
    restartPortalTimeout();
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
  return LittleFS.exists("/wifi.json") && !ssid.isEmpty();
}

void AyresWiFiManager::loadCredentials() {
  if (!LittleFS.exists("/wifi.json")) {
    AYLOG_I("ℹ️ /wifi.json no existe.");
    return;
  }
  File file = LittleFS.open("/wifi.json", "r");
  if (!file) {
    _lastError = Error::STORAGE_ERROR;
    AYLOG_E("❌ No se pudo abrir /wifi.json");
    return;
  }
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    _lastError = Error::STORAGE_ERROR;
    AWM_LOGE("❌ Error al deserializar JSON de /wifi.json");
    return;
  }

  bool needsEncryptionMigration = false;
  String loadedSsid, loadedPassword;

  if (doc.is<const char *>()) {
    // Formato actual: una única cadena opaca con el sobre autenticado.
    if (!_aesKeyConfigured) {
      _lastError = Error::ENCRYPTION_ERROR;
      AYLOG_E("❌ Encrypted credentials require "
              "enableCredentialEncryption() first.");
      return;
    }
    if (!decryptCredentialEnvelope(doc.as<String>(), loadedSsid,
                                   loadedPassword)) {
      _lastError = Error::ENCRYPTION_ERROR;
      AYLOG_E("❌ No se pudo autenticar el archivo de credenciales");
      return;
    }
    needsEncryptionMigration = !_encryptionEnabled;
    AYLOG_I("Credenciales cifradas cargadas");
  } else if (doc["encrypted"].as<bool>()) {
    // Compatibilidad de lectura con los objetos cifrados anteriores.
    if (!_aesKeyConfigured) {
      _lastError = Error::ENCRYPTION_ERROR;
      AYLOG_E("❌ Encrypted credentials require "
              "enableCredentialEncryption() first.");
      return;
    }
    loadedSsid = decryptString(doc["ssid"].as<String>());
    loadedPassword = decryptString(doc["password"].as<String>());
    if (loadedSsid.isEmpty()) {
      _lastError = Error::ENCRYPTION_ERROR;
      AYLOG_E("❌ No se pudieron migrar las credenciales cifradas");
      return;
    }
    needsEncryptionMigration = true;
    AYLOG_I("Formato cifrado anterior detectado; se migrará");
  } else {
    // Formato plano: solamente los dos campos necesarios.
    loadedSsid = doc["ssid"].as<String>();
    loadedPassword = doc["password"].as<String>();
    AYLOG_I("Credenciales en texto plano cargadas");
    needsEncryptionMigration = _encryptionEnabled;
  }

  if (loadedSsid.isEmpty()) {
    _lastError = Error::STORAGE_ERROR;
    AYLOG_W("SSID vacío en el archivo de credenciales");
    return;
  }
  ssid = loadedSsid;
  password = loadedPassword;
  if (needsEncryptionMigration) {
    AYLOG_I("Migrando credenciales al sobre cifrado actual");
    if (!saveCredentials(ssid, password))
      return;
  }
  _lastError = Error::NONE;
  AYLOG_I("✅ Credenciales Wi-Fi cargadas");
}

bool AyresWiFiManager::saveCredentials(String s, String p) {
  StaticJsonDocument<512> doc;

  if (_encryptionEnabled) {
    // El archivo no expone estructura, nombres de campos ni algoritmo.
    String envelope = encryptCredentialEnvelope(s, p);
    if (envelope.isEmpty()) {
      _lastError = Error::ENCRYPTION_ERROR;
      AYLOG_E("No se pudieron cifrar las credenciales");
      return false;
    }
    doc.set(envelope);
    AYLOG_I("Guardando credenciales cifradas");
  } else {
    // El formato plano es deliberadamente simple y legible.
    doc["ssid"] = s;
    doc["password"] = p;
    AYLOG_I("Guardando credenciales en texto plano");
  }

  File file = LittleFS.open("/wifi.json", "w");
  if (!file) {
    _lastError = Error::STORAGE_ERROR;
    AYLOG_E("❌ Error abriendo /wifi.json para escritura");
    return false;
  }
  if (serializeJson(doc, file) == 0) {
    file.close();
    _lastError = Error::STORAGE_ERROR;
    AYLOG_E("Error escribiendo /wifi.json");
    return false;
  }
  file.close();
  _lastError = Error::NONE;
  return true;
}

void AyresWiFiManager::eraseCredentials() {
  if (eraseWiFiCredentials())
    AYLOG_I("Credenciales Wi-Fi eliminadas");
  else
    AYLOG_W("No se pudo eliminar /wifi.json");
}

bool AyresWiFiManager::eraseWiFiCredentials() {
  if (!LittleFS.exists("/wifi.json"))
    return true;

  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (LittleFS.remove("/wifi.json")) {
      ssid = "";
      password = "";
      return true;
    }
    AWM_sleep_ms(50);
  }
  return false;
}

/* ================================= CONEXIÓN STA
 * ================================ */
bool AyresWiFiManager::connectToWiFi() {
  if (!tieneCredenciales()) {
    _state = State::OFFLINE;
    _lastError = Error::NO_CREDENTIALS;
    return false;
  }

  _state = State::WIFI_CONNECTING;
  _lastError = Error::NONE;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  AYLOG_I("Conectando a %s", ssid.c_str());

  const uint32_t TOUT_MS = 15000;
  uint32_t t0 = AWM_now_ms();
  while (AWM_now_ms() - t0 < TOUT_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      AYLOG_I("Conectado. IP: %s", WiFi.localIP().toString().c_str());
      WiFi.setSleep(false);
      connected = true;
      _state = State::WIFI_CONNECTED;
      _lastError = Error::NONE;
      return true;
    }
    AWM_sleep_ms(250);
    // Alimentar watchdog durante el intento de conexión inicial
    esp_task_wdt_reset();
  }

  AYLOG_W("⏱️ Tiempo agotado. No se pudo conectar.");
  connected = false;
  _state = State::OFFLINE;
  _lastError = Error::CONNECTION_TIMEOUT;
  return false;
}

bool AyresWiFiManager::isConnected() {
  connected = (WiFi.status() == WL_CONNECTED);
  if (connected) {
    if (_state == State::OFFLINE || _state == State::WIFI_CONNECTING)
      _state = State::WIFI_CONNECTED;
  } else if (_state != State::WIFI_CONNECTING) {
    _state = State::OFFLINE;
  }
  return connected;
}

AyresWiFiManager::State AyresWiFiManager::getState() const {
  if (portalActive)
    return State::PORTAL_ACTIVE;
  return _state;
}

const char *AyresWiFiManager::stateToString(State state) {
  switch (state) {
  case State::OFFLINE:
    return "OFFLINE";
  case State::WIFI_CONNECTING:
    return "WIFI_CONNECTING";
  case State::WIFI_CONNECTED:
    return "WIFI_CONNECTED";
  case State::INTERNET_OK:
    return "INTERNET_OK";
  case State::NO_INTERNET:
    return "NO_INTERNET";
  case State::PORTAL_ACTIVE:
    return "PORTAL_ACTIVE";
  }
  return "UNKNOWN";
}

AyresWiFiManager::Error AyresWiFiManager::getLastError() const {
  return _lastError;
}

const char *AyresWiFiManager::errorToString(Error error) {
  switch (error) {
  case Error::NONE:
    return "NONE";
  case Error::NO_CREDENTIALS:
    return "NO_CREDENTIALS";
  case Error::CONNECTION_TIMEOUT:
    return "CONNECTION_TIMEOUT";
  case Error::INTERNET_UNREACHABLE:
    return "INTERNET_UNREACHABLE";
  case Error::STORAGE_ERROR:
    return "STORAGE_ERROR";
  case Error::ENCRYPTION_ERROR:
    return "ENCRYPTION_ERROR";
  }
  return "UNKNOWN";
}

uint32_t AyresWiFiManager::getReconnectCount() const {
  return _reconnectCount;
}

uint32_t AyresWiFiManager::getLastInternetCheck() const {
  return _lastInternetCheck;
}

int AyresWiFiManager::getSignalStrength() { return WiFi.RSSI(); }

void AyresWiFiManager::reintentarConexionSiNecesario() {
  if (!autoReconnect)
    return;

  // ESTADO INTERNO (Static para mantener independencia del .h)
  // Permite reconexión NO BLOQUEANTE.
  static enum { RE_IDLE, RE_CONNECTING, RE_WAITING } reState = RE_IDLE;
  static uint32_t reStart = 0;

  // Si ya estamos conectados (por cualquier medio), reset y salir
  if (WiFi.status() == WL_CONNECTED) {
    if (!connected) {
      connected = true;
      _state = State::WIFI_CONNECTED;
      _lastError = Error::NONE;
      AYLOG_I("🔌 Conexión recuperada.");
      sincronizarHoraNTP();
    }
    reState = RE_IDLE;
    return;
  }

  // FIX: Si hay clientes en el AP, permitir reconexión pero "muy relajada"
  // (Throttled) para no saturar la radio y causar resets TG1WDT. En lugar de
  // bloquear, forzamos un backoff dinámico mayor (ej. 60s).
  uint32_t effectiveBackoff = reconnectBackoffMs;
  if (WiFi.softAPgetStationNum() > 0) {
    if (effectiveBackoff < 60000)
      effectiveBackoff = 60000;
  }

  connected = false;
  uint32_t ahora = AWM_now_ms();

  // Máquina de estados ASÍNCRONA
  switch (reState) {
  case RE_IDLE:
    // Respetar tiempo de backoff efectivo
    if (ahora - ultimoIntentoWiFi >= effectiveBackoff) {
      reState = RE_CONNECTING;
    }
    break;

  case RE_CONNECTING:
    if (!ssid.isEmpty() && !password.isEmpty()) {
      AYLOG_I("🔁 Intentando reconexión WiFi (ASYNC)... (ventana=%lu ms)",
              (unsigned long)reconnectAttemptMs);

      // FIX: Evitar cambio de modo si ya estamos en el correcto (ahorra tiempo
      // CPU/Radio)
      const wifi_mode_t curMode = WiFi.getMode();
      const wifi_mode_t targetMode =
          (portalActive || externalApActive) ? WIFI_AP_STA : WIFI_STA;

      if (curMode != targetMode) {
        WiFi.mode(targetMode);
        AWM_sleep_ms(50); // Yield tras cambio de modo
      }

      // Iniciamos conexión SIN bloquear
      WiFi.begin(ssid.c_str(), password.c_str());
      _state = State::WIFI_CONNECTING;
      _reconnectCount++;

      // FIX: Yield extra para dar aire al stack WiFi
      AWM_sleep_ms(10);

      reStart = ahora;
      reState = RE_WAITING;
    } else {
      // Sin credenciales, volvemos a idle
      reState = RE_IDLE;
    }
    break;

  case RE_WAITING:
    // 1. Verificar éxito
    if (WiFi.status() == WL_CONNECTED) {
      AYLOG_I("🔌 Reconectado a WiFi.");
      sincronizarHoraNTP();
      connected = true;
      _state = State::WIFI_CONNECTED;
      _lastError = Error::NONE;
      failCount = 0;
      failWindowStart = 0;
      reState = RE_IDLE;
      return;
    }

    // 2. Verificar Timeout
    if (ahora - reStart > reconnectAttemptMs) {
      AYLOG_W("❌ Reconexión WiFi fallida (Timeout).");
      _state = State::OFFLINE;
      _lastError = Error::CONNECTION_TIMEOUT;
      ultimoIntentoWiFi = ahora; // Iniciar backoff
      reState = RE_IDLE;

      // Lógica SMART RETRIES (Mantenida idéntica)
      if (fallbackPolicy == FallbackPolicy::SMART_RETRIES) {
        if (failWindowStart == 0 || (ahora - failWindowStart) > failWindowMs) {
          failWindowStart = ahora;
          failCount = 0;
        }
        failCount++;
        AYLOG_D("📉 SMART: fallos=%u/%u en %lu ms", failCount, maxFailRetries,
                (unsigned long)(ahora - failWindowStart));
        if (failCount >= maxFailRetries) {
          AYLOG_W("🚪 SMART: abriendo portal por fallos acumulados");
          startPortal();
          failCount = 0;
          failWindowStart = 0;
        }
      }
    }
    // Si no conecta ni da timeout, seguimos en WAITING (retorna al loop)
    esp_task_wdt_reset();
    break;
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

  const wifi_mode_t curMode = WiFi.getMode();
  const wifi_mode_t targetMode =
      (portalActive || externalApActive) ? WIFI_AP_STA : WIFI_STA;

  if (curMode != targetMode) {
    WiFi.mode(targetMode);
    AWM_sleep_ms(50);
  }

  WiFi.begin(ssid.c_str(), password.c_str());
  _state = State::WIFI_CONNECTING;
  _reconnectCount++;
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
  const char *tz = "UTC0";
  configTzTime(tz, "time.google.com", "time.cloudflare.com", "pool.ntp.org");
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
  }

  // Fallback por HTTP Date si NTP no responde.
  AYLOG_W("🌐 NTP lento/bloqueado; intento fallback por HTTP Date…");
  if (AWM_syncTimeFromHttp_("http://google.com", 6000) ||
      AWM_syncTimeFromHttp_("http://worldtimeapi.org/api/ip", 8000)) {
    return;
  }
  AYLOG_W("⚠️ No pude sincronizar hora (NTP/HTTP). Reintentaré luego.");
}

uint64_t AyresWiFiManager::getTimestamp() {
  time_t now = time(nullptr);
  return (now > 100000) ? static_cast<uint64_t>(now) * 1000ULL : 0;
}

/* =============================== INTERNET CHECK
 * =============================== */
bool AyresWiFiManager::hayInternet() {
  _lastInternetCheck = AWM_now_ms();
  if (WiFi.status() != WL_CONNECTED) {
    _state = State::OFFLINE;
    _lastError = Error::INTERNET_UNREACHABLE;
    return false;
  }
  WiFiClient client;
  HTTPClient http;
  http.begin(client, "http://clients3.google.com/generate_204");
  http.setConnectTimeout(3000);
  int httpCode = http.GET();
  http.end();
  const bool online = (httpCode == 204);
  _state = online ? State::INTERNET_OK : State::NO_INTERNET;
  _lastError = online ? Error::NONE : Error::INTERNET_UNREACHABLE;
  return online;
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

  LedPattern want;

  // El escaneo tiene prioridad porque ocupa temporalmente la radio.
  if (scanning) {
    want = LedPattern::BLINK_FAST;
  } else {
    switch (getState()) {
    case State::OFFLINE:
      want = LedPattern::BLINK_SLOW;
      break;
    case State::WIFI_CONNECTING:
      want = LedPattern::BLINK_FAST;
      break;
    case State::WIFI_CONNECTED:
    case State::INTERNET_OK:
      want = LedPattern::ON;
      break;
    case State::NO_INTERNET:
      want = LedPattern::BLINK_DOUBLE;
      break;
    case State::PORTAL_ACTIVE:
      want = LedPattern::BLINK_TRIPLE;
      break;
    default:
      want = LedPattern::OFF;
      break;
    }
  }

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

void AyresWiFiManager::eraseJsonInDir(const char *dirPath,
                                      bool respectProtected,
                                      EraseResult &result) {
  if (!dirPath || !*dirPath)
    return;

  // Primero se recopilan las rutas. Así no queda ningún File abierto por AWM
  // durante la fase de borrado.
  std::vector<String> pendingDirs;
  std::vector<String> jsonFiles;
  pendingDirs.push_back(String(dirPath));

  while (!pendingDirs.empty()) {
    const String currentDir = pendingDirs.back();
    pendingDirs.pop_back();

    File dir = LittleFS.open(currentDir);
    if (!dir || !dir.isDirectory()) {
      if (dir)
        dir.close();
      continue;
    }

    String base = currentDir;
    if (!base.endsWith("/"))
      base += "/";

    for (File file = dir.openNextFile(); file; file = dir.openNextFile()) {
      String full = file.name();
      if (!full.startsWith("/"))
        full = base + full;
      const bool directory = file.isDirectory();
      file.close();

      if (directory) {
        pendingDirs.push_back(full);
      } else {
        String lower = full;
        lower.toLowerCase();
        if (lower.endsWith(".json") &&
            (!respectProtected || !isProtectedJson(full))) {
          jsonFiles.push_back(full);
        }
      }
    }
    dir.close();
  }

  result.found += jsonFiles.size();
  for (const String &path : jsonFiles) {
    bool removed = false;
    for (uint8_t attempt = 0; attempt < 3 && !removed; ++attempt) {
      removed = LittleFS.remove(path);
      if (!removed)
        AWM_sleep_ms(50);
    }

    if (removed) {
      result.removed++;
      AWM_LOGI("Borrado JSON: %s", path.c_str());
    } else {
      result.failed++;
      AWM_LOGW("No se pudo borrar JSON: %s", path.c_str());
    }

    esp_task_wdt_reset();
    if (_busyCallback)
      _busyCallback();
  }

  if (result.failed > 0) {
    AYLOG_W("Borrado JSON incompleto: %u eliminados, %u fallidos",
            result.removed, result.failed);
  } else {
    AYLOG_I("Borrado JSON completo: %u archivo(s)", result.removed);
  }
}

/* ====================== Portal timeout por esp_timer ====================== */
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
