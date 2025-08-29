/*
 *  SPDX-License-Identifier: MIT
 *  AyresWiFiManager — Implementación (Source)
 *  ---------------------------------------------------------------
 *  @file      AyresWiFiManager.cpp
 *  @versión   2.0.2
 *  @autor     Daniel C. Salgado — AyresNet
 *  @licencia  MIT
 *  @proyecto  https://github.com/AyresNet/AyresWiFiManager
 *
 *  @resumen
 *    Implementación multiplataforma (ESP32/ESP8266) del gestor Wi-Fi con:
 *    portal cautivo (AP + DNS catch-all), almacenamiento en LittleFS,
 *    políticas de fallback, botón de provisión, LED de estados, NTP opcional
 *    y verificación de Internet (generate_204).
 *
 *  Notas de implementación
 *  ---------------------------------------------------------------
 *  • ESP32
 *      - Ahorro de energía deshabilitado: esp_wifi_set_ps(WIFI_PS_NONE).
 *      - LittleFS.begin(true) → auto-formatea si falla el montaje.
 *      - Borrado de JSON: cerrar File antes de unlink; recursivo por carpeta.
 *
 *  • ESP8266
 *      - Sleep deshabilitado: WiFi.setSleepMode(WIFI_NONE_SLEEP).
 *      - Borrado de JSON no recursivo: eraseJsonInDir() itera por carpeta.
 *
 *  Estados del LED (pin por defecto 2)
 *      ON             → conectado
 *      BLINK_SLOW     → portal activo
 *      BLINK_FAST     → escaneando
 *      BLINK_DOUBLE   → feedback durante hold 2–5 s (abrirá portal)
 *      BLINK_TRIPLE   → feedback durante hold ≥5 s (borrará credenciales)
 *      OFF            → sin enlace y sin portal
 *
 *  Botón (pin 0, INPUT_PULLUP, activo en LOW)
 *      2–5 s  → abre portal (si enableButtonPortal(true))
 *      ≥5 s   → borra credenciales (respetando setProtectedJsons) y reinicia
 *
 *  Portal cautivo
 *      • setCaptivePortal(true) habilita DNS catch-all y rutas “captive”.
 *      • setPortalTimeout(seg) cierre por inactividad.
 *      • setAPClientCheck(true) evita cierre si hay clientes en el AP.
 *      • setWebClientCheck(true) reinicia el timeout por cada request HTTP.
 *
 *  Borrado seguro de .json
 *      • eraseJsonInDir("/") elimina todos los .json excepto los protegidos
 *        mediante setProtectedJsons({...}). En ESP32 es recursivo; en ESP8266,
 *        llamar por carpeta si hay subdirectorios.
 *
 *  Novedades v2.0.1
 *  ---------------------------------------------------------------
 *  • Reconexión configurable:
 *      - setReconnectBackoffMs(ms) → retraso mínimo entre reintentos (por defecto 10 s)
 *      - setReconnectAttemptMs(ms) → ventana por intento (por defecto 5 s)
 *  • Convivencia con AP/portal externo:
 *      - setExternalApActive(true) habilita escenarios AP+STA sin bajar el SoftAP
 *        externo durante los reintentos; el portal **no se cierra** de forma
 *        inadvertida si hay un AP/portal externo activo.
 *      - stopPortal() preserva el SoftAP cuando el flag externo está activo.
 *      - forzarReconexion() y reintentarConexionSiNecesario() respetan dicho flag.
 *
 *  Consideraciones de rendimiento
 *      - Evitar escaneos demasiado frecuentes (SCAN_INTERVAL_MS).
 *      - Cachear el resultado de /scan(.json) si la ventana es corta.
 *      - Ajustar patrones del LED para minimizar jitter en WiFi/HTTP.
 */


#include "AyresWiFiManager.h"
#include <ArduinoJson.h>
#include "AWM_Logging.h"

#if defined(ESP32)
  #include <HTTPClient.h>
  #include <esp_wifi.h>
#elif defined(ESP8266)
  #include <ESP8266HTTPClient.h>
#endif

#include <time.h>

// ---------- ctor ----------
AyresWiFiManager::AyresWiFiManager(uint8_t ledPin_, uint8_t buttonPin_)
: server(80), ledPin(ledPin_), buttonPin(buttonPin_) {}

// =====================================================
//                 SETTERS / TOGGLES
// =====================================================
void AyresWiFiManager::setHtmlPathPrefix(const String& prefix) {
  htmlPathPrefix = prefix.endsWith("/") ? prefix : prefix + "/";
}

void AyresWiFiManager::setHostname(const String& host){ hostname = host; }

void AyresWiFiManager::setAPCredentials(const String& ssid_, const String& pass_){
  apSSID = ssid_; apPASS = pass_;
}

void AyresWiFiManager::setCaptivePortal(bool enabled){ captiveEnabled = enabled; }
void AyresWiFiManager::setPortalTimeout(uint32_t seconds){ portalTimeoutMs = seconds * 1000UL; }
void AyresWiFiManager::setAPClientCheck(bool enabled){ apClientCheck = enabled; }
void AyresWiFiManager::setWebClientCheck(bool enabled){ webClientCheck = enabled; }
bool AyresWiFiManager::isPortalActive() const { return portalActive; }
void AyresWiFiManager::openPortal(){ startPortal(); }
void AyresWiFiManager::closePortal(){ stopPortal(); }

void AyresWiFiManager::setFallbackPolicy(FallbackPolicy p){ fallbackPolicy = p; }
void AyresWiFiManager::setSmartRetries(uint8_t maxRetries, uint32_t windowMs){
  maxFailRetries = maxRetries; failWindowMs = windowMs;
}
void AyresWiFiManager::enableButtonPortal(bool enable){ allowButtonPortal = enable; }

// ===== [NEW] Reconexión configurable =====
void AyresWiFiManager::setReconnectBackoffMs(uint32_t ms){
  reconnectBackoffMs = (ms < 1000) ? 1000 : ms; // sanity min 1s
  AWM_LOGI("⚙️  Backoff de reconexión = %lu ms", (unsigned long)reconnectBackoffMs);
}
void AyresWiFiManager::setReconnectAttemptMs(uint32_t ms){
  reconnectAttemptMs = (ms < 1000) ? 1000 : ms; // min 1s
  AWM_LOGI("⚙️  Ventana de intento = %lu ms", (unsigned long)reconnectAttemptMs);
}
// ===== [NEW] AP/portal externo =====
void AyresWiFiManager::setExternalApActive(bool active){
  externalApActive = active;
  AWM_LOGI("⚙️  AP externo activo: %s", externalApActive ? "sí" : "no");
}
bool AyresWiFiManager::isExternalApActive() const { return externalApActive; }

// =====================================================
//                      BEGIN / RUN
// =====================================================
void AyresWiFiManager::begin() {
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(buttonPin, INPUT_PULLUP);

#if defined(ESP32)
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
    AWM_LOGE("❌ Error montando LittleFS");
    return;
  }

  loadCredentials();
}

void AyresWiFiManager::run() {
  // Ventana para detectar hold con feedback LED
  AWM_LOGI("🔔 Botón: 2–5s abre portal | ≥5s borra credenciales");

  unsigned long startTime = millis();
  bool pressed = false;

  ledSet(LedPattern::BLINK_SLOW); // guiño durante ventana
  while (millis() - startTime < 2000) {
    if (digitalRead(buttonPin) == LOW) { pressed = true; break; }
    ledTask(); delay(10);
  }
  ledSet(LedPattern::OFF);

  if (pressed) {
    unsigned long t0 = millis();
    while (digitalRead(buttonPin) == LOW) {
      unsigned long held = millis() - t0;
      if (held >= 5000) {
        setLedPatternManual(LedPattern::BLINK_TRIPLE);
        AWM_LOGW("🩹 Hold ≥5s → borrar credenciales y reiniciar");
        eraseCredentials();
        delay(900);
        ESP.restart();
      } else if (held >= 2000) {
        setLedPatternManual(LedPattern::BLINK_DOUBLE); // avisar: abriré portal
      } else {
        setLedPatternManual(LedPattern::BLINK_FAST);   // hold corto
      }
      ledTask(); delay(10);
    }
    unsigned long held = millis() - t0;
    if (held >= 2000 && held < 5000 && allowButtonPortal) {
      AWM_LOGI("🟢 Hold 2–5s → abrir portal");
      setLedAuto(true);
      startPortal();
      return;
    }
    setLedAuto(true);
  }

  // Conectar si hay credenciales
  if (connectToWiFi()) {
    AWM_LOGI("✅ Conexión WiFi exitosa.");
    sincronizarHoraNTP();
    ledSet(LedPattern::ON);
    connected = true;
    return;
  }

  // No conectó → actuar según política
  switch (fallbackPolicy) {
    case FallbackPolicy::ON_FAIL:
      AWM_LOGI("🟡 Conexión fallida → abriendo portal (policy=ON_FAIL)");
      startPortal();
      break;
    case FallbackPolicy::NO_CREDENTIALS_ONLY:
      if (!tieneCredenciales()) {
        AWM_LOGI("🟡 Sin credenciales → abriendo portal");
        startPortal();
      } else {
        AWM_LOGI("🟠 Con credenciales → NO abrir portal (NO_CREDENTIALS_ONLY)");
      }
      break;
    case FallbackPolicy::SMART_RETRIES:
      AWM_LOGI("🟠 SMART_RETRIES activo → sin portal por ahora; se abrirá si fallan varios intentos");
      break;
    case FallbackPolicy::BUTTON_ONLY:
      AWM_LOGI("🟠 BUTTON_ONLY → no abrir portal automáticamente");
      break;
    case FallbackPolicy::NEVER:
      AWM_LOGI("🟠 NEVER → no abrir portal automáticamente");
      break;
  }
}

// =====================================================
void AyresWiFiManager::update() {
  server.handleClient();
  if (dnsRunning) dns.processNextRequest();

  ledAutoUpdate();
  ledTask();

  if (portalActive && portalHasTimedOut()) {
    AWM_LOGW("⏳ Portal tiempo agotado → cerrando");
    stopPortal();
  }
}

// =====================================================
//                    AP / DNS / HTTP
// =====================================================
void AyresWiFiManager::redirectToRoot(){
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void AyresWiFiManager::setupHTTPRoutes(){
  // Páginas propias
  server.on("/",      std::bind(&AyresWiFiManager::handleRoot, this));
  server.on("/save",  std::bind(&AyresWiFiManager::handleSave, this));

  // Escaneo: principal y alias clásico
  server.on("/scan",      std::bind(&AyresWiFiManager::handleScan, this));
  server.on("/scan.json", std::bind(&AyresWiFiManager::handleScan, this));

  // NUEVO: borrar credenciales vía POST /erase
  server.on("/erase", HTTP_POST, std::bind(&AyresWiFiManager::handleErase, this));

  // Rutas de detección de conectividad: forzar portal
  if (captiveEnabled) {
    server.on("/generate_204",        [this](){ redirectToRoot(); }); // Android
    server.on("/gen_204",             [this](){ redirectToRoot(); }); // Android alt
    server.on("/hotspot-detect.html", [this](){ redirectToRoot(); }); // iOS/macOS
    server.on("/connecttest.txt",     [this](){ redirectToRoot(); }); // Windows
    server.on("/ncsi.txt",            [this](){ redirectToRoot(); }); // Windows NCSI
    server.on("/fwlink",              [this](){ redirectToRoot(); }); // Windows IE/Edge
  }
  server.on("/favicon.ico",         [this](){ server.send(204, "text/plain", ""); });

  // Cualquier otra ruta → 302 al root
  server.onNotFound(std::bind(&AyresWiFiManager::handleNotFound, this));
}

void AyresWiFiManager::startDNS(){
  if (dnsRunning) return;
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP()); // usar la IP real del AP
  dnsRunning = true;
}

void AyresWiFiManager::stopDNS(){
  if (!dnsRunning) return;
  dns.stop();
  dnsRunning = false;
}

void AyresWiFiManager::setupAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apGW, apSN);
  WiFi.softAP(apSSID.c_str(), apPASS.c_str());
#if defined(ESP32)
  if (hostname.length()) WiFi.softAPsetHostname(hostname.c_str());
#endif
  AWM_LOGI("📡 AP: %s | IP %s", apSSID.c_str(), apIP.toString().c_str());
}

void AyresWiFiManager::startPortal(){
  if (portalActive) return;
  setupAP();
  setupHTTPRoutes();
  server.begin();
  if (captiveEnabled) {
    startDNS();          // DNS catch-all activo
  } else {
    if (dnsRunning) stopDNS(); // por las dudas
  }
  portalActive    = true;
  portalStart     = millis();
  lastHttpAccess  = portalStart;
  AWM_LOGI("🌐 Portal cautivo activo en 192.168.4.1 (GET /, /scan, POST /save, POST /erase)");
  ledSet(LedPattern::BLINK_SLOW);
}

void AyresWiFiManager::stopPortal(){
  if (!portalActive) return;
  stopDNS();
  server.stop();

  // [CHANGED] Si hay AP externo activo, NO bajamos el SoftAP global.
  if (!externalApActive) {
    WiFi.softAPdisconnect(true);
  } else {
    AWM_LOGI("🔒 AP externo activo → preservo SoftAP (no se desconecta).");
  }

  portalActive = false;

  // [CHANGED] Restaurar modo según contexto:
  if (externalApActive) {
    // Dejar radio en AP activo (el externo gestiona su web)
    WiFi.mode(WIFI_AP);
  } else {
    if (!ssid.isEmpty()) WiFi.mode(WIFI_STA);
    else                 WiFi.mode(WIFI_OFF);
  }

  AWM_LOGI("✅ Portal cautivo detenido");
}

bool AyresWiFiManager::captivePortalRedirect(){
  if (!portalActive || !captiveEnabled) return false;

  String host = server.hostHeader();
  String ap   = WiFi.softAPIP().toString();
  if (ap == "0.0.0.0") ap = apIP.toString();

  if (host != ap){
    server.sendHeader("Location", String("http://") + ap, true);
    server.send(302, "text/plain", "");
    server.client().stop();
    return true;
  }
  return false;
}

uint8_t AyresWiFiManager::softAPStationCount(){
  return WiFi.softAPgetStationNum();
}

bool AyresWiFiManager::portalHasTimedOut(){
  if (portalTimeoutMs == 0) return false;

  if (apClientCheck && softAPStationCount() > 0){
    portalStart = millis(); // estira ventana mientras haya clientes
    return false;
  }

  unsigned long base = webClientCheck ? lastHttpAccess : portalStart;
  return (millis() - base) > portalTimeoutMs;
}

// ---------- HTTP ----------
void AyresWiFiManager::handleRoot() {
  if (captivePortalRedirect()) return;
  lastHttpAccess = millis();

  String path = htmlPathPrefix + "index.html";
  if (!LittleFS.exists(path)) {
    server.send(500, "text/html", "<h1>Error: index.html no encontrado</h1>");
    return;
  }
  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) {
    server.send(500, "text/html", "<h1>Error abriendo index.html</h1>");
    return;
  }
  server.send(200, "text/html", file.readString());
  file.close();
}

void AyresWiFiManager::handleSave() {
  if (captivePortalRedirect()) return;
  lastHttpAccess = millis();

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
  doc["ssid"]     = inSsid;
  doc["password"] = inPass;

  File file = LittleFS.open("/wifi.json", "w");
  if (!file) {
    mostrarPaginaError("Error al guardar credenciales.");
    return;
  }
  serializeJson(doc, file);
  file.close();

  File success = LittleFS.open(htmlPathPrefix + "success.html", "r");
  if (!success) {
    server.send(200, "text/html", "<h1>Guardado. Reiniciando...</h1>");
  } else {
    server.send(200, "text/html", success.readString());
    success.close();
  }

  delay(1000);
  ESP.restart();
}

void AyresWiFiManager::handleErase() {
  if (captivePortalRedirect()) return;
  lastHttpAccess = millis();

  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Método no permitido");
    return;
  }

  // Confirmamos primero al navegador para evitar timeouts
  server.send(200, "application/json", "{\"ok\":true}");
  delay(150);

  // Borra todos los .json (respetando la lista blanca setProtectedJsons)
  eraseCredentials();

  delay(300);
  ESP.restart();
}

void AyresWiFiManager::handleScan() {
  lastHttpAccess = millis();
  AWM_LOGI("🔍 Escaneando redes WiFi (SYNC, AP+STA)…");

  // Mantener el AP mientras el STA escanea
  WiFi.mode(WIFI_AP_STA);
  delay(50);

  // Si había un scan en curso, cancelalo
  int st = WiFi.scanComplete();
  if (st == WIFI_SCAN_RUNNING) {
    WiFi.scanDelete();
  }

  // Estado LED de "scanning"
  scanning = true;
  scanningUntil = millis() + 1500;

  // Escaneo bloqueante (fiable y simple)
  int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);
  if (n < 0) {
    scanning = false;
    server.send(200, "application/json", "[]");
    AWM_LOGW("⚠️ Escaneo falló, devolviendo []");
    return;
  }

  // Construir JSON [{ssid, rssi, secure}, ...]
  size_t cap = 64U + (size_t)n * 64U;
  if (cap < 512U) cap = 512U;
  DynamicJsonDocument doc(cap);
  JsonArray arr = doc.to<JsonArray>();

  for (int i = 0; i < n; ++i) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue; // ocultos
    JsonObject o = arr.createNestedObject();
    o["ssid"]   = s;
    o["rssi"]   = WiFi.RSSI(i);
    o["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
  }

  WiFi.scanDelete(); // limpiar resultados en RAM
  scanning = false;

  String out;
  serializeJson(arr, out);
  server.send(200, "application/json", out);
  AWM_LOGI("✅ Escaneo OK: %d redes", (int)arr.size());
}

void AyresWiFiManager::handleNotFound() {
  if (captivePortalRedirect()) return;
  lastHttpAccess = millis();
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void AyresWiFiManager::mostrarPaginaError(const String& mensajeFallback) {
  File errorFile = LittleFS.open(htmlPathPrefix + "error.html", "r");
  if (!errorFile) {
    server.send(500, "text/html", "<h1>Error: " + mensajeFallback + "</h1>");
  } else {
    server.send(500, "text/html", errorFile.readString());
    errorFile.close();
  }
}

// =====================================================
//                    CREDENCIALES
// =====================================================
bool AyresWiFiManager::tieneCredenciales() const {
  return LittleFS.exists("/wifi.json") && !ssid.isEmpty() && !password.isEmpty();
}

void AyresWiFiManager::loadCredentials() {
  if (!LittleFS.exists("/wifi.json")) {
    AWM_LOGI("ℹ️ /wifi.json no existe.");
    return;
  }
  File file = LittleFS.open("/wifi.json", "r");
  if (!file) {
    AWM_LOGE("❌ No se pudo abrir /wifi.json");
    return;
  }
  StaticJsonDocument<192> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    AWM_LOGE("❌ Error al deserializar JSON de /wifi.json");
    return;
  }

  String loadedSsid = doc["ssid"].as<String>();
  String loadedPassword = doc["password"].as<String>();
  if (loadedSsid.isEmpty() || loadedPassword.isEmpty()) {
    AWM_LOGW("⚠️ Credenciales vacías en archivo.");
    return;
  }
  ssid     = loadedSsid;
  password = loadedPassword;
  AWM_LOGI("✅ Credenciales cargadas (SSID=\"%s\").", ssid.c_str());
}

void AyresWiFiManager::saveCredentials(String s, String p) {
  StaticJsonDocument<192> doc;
  doc["ssid"]     = s;
  doc["password"] = p;
  File file = LittleFS.open("/wifi.json", "w");
  if (!file) {
    AWM_LOGE("❌ Error abriendo /wifi.json para escritura");
    return;
  }
  serializeJson(doc, file);
  file.close();
}

void AyresWiFiManager::eraseCredentials() {
  eraseJsonInDir("/");   // raíz

  #if defined(ESP8266)
    // En ESP8266 el iterador no es recursivo; llamar por subcarpetas si hace falta.
  #endif

  AWM_LOGI("🧹 Limpieza de .json finalizada (respetando protegidos).");
}

// =====================================================
//                     CONEXIÓN STA
// =====================================================
bool AyresWiFiManager::connectToWiFi() {
  if (!tieneCredenciales()) return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  AWM_LOGI("Conectando a %s", ssid.c_str());

  const uint32_t TOUT_MS = 15000;
  uint32_t t0 = millis();
  while (millis() - t0 < TOUT_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      AWM_LOGI("Conectado. IP: %s", WiFi.localIP().toString().c_str());
#if defined(ESP32)
      WiFi.setSleep(false);
#endif
      connected = true;
      return true;
    }
    delay(250);
  }

  AWM_LOGW("⏱️ Tiempo agotado. No se pudo conectar.");
  connected = false;
  return false;
}

bool AyresWiFiManager::isConnected() {
  connected = (WiFi.status() == WL_CONNECTED);
  return connected;
}

int AyresWiFiManager::getSignalStrength() {
  return WiFi.RSSI();
}

void AyresWiFiManager::reintentarConexionSiNecesario() {
  if (!autoReconnect) return;
  if (WiFi.status() == WL_CONNECTED){ connected = true; return; }

  connected = false;
  unsigned long ahora = millis();

  // [CHANGED] Backoff configurable
  if (ahora - ultimoIntentoWiFi < reconnectBackoffMs) return;
  ultimoIntentoWiFi = ahora;

  if (!ssid.isEmpty() && !password.isEmpty()) {
    AWM_LOGI("🔁 Intentando reconexión WiFi... (ventana=%lu ms, backoff=%lu ms)",
             (unsigned long)reconnectAttemptMs, (unsigned long)reconnectBackoffMs);

    // [CHANGED] Si hay portal AWM o AP externo, mantener AP activo durante el intento
    if (portalActive || externalApActive) WiFi.mode(WIFI_AP_STA);
    else                                  WiFi.mode(WIFI_STA);

    WiFi.begin(ssid.c_str(), password.c_str());
    uint32_t t0 = millis();
    bool ok = false;

    // [CHANGED] Ventana configurable
    while (millis() - t0 < reconnectAttemptMs) {
      if (WiFi.status() == WL_CONNECTED) { ok = true; break; }
      delay(250);
    }
    if (ok) {
      AWM_LOGI("🔌 Reconectado a WiFi.");
      sincronizarHoraNTP();
      connected = true;
      failCount = 0; failWindowStart = 0;
      return;
    }
    AWM_LOGW("❌ Reconexión WiFi fallida.");

    // SMART_RETRIES (sin cambios)
    if (fallbackPolicy == FallbackPolicy::SMART_RETRIES) {
      if (failWindowStart == 0 || (millis() - failWindowStart) > failWindowMs) {
        failWindowStart = millis();
        failCount = 0;
      }
      failCount++;
      AWM_LOGD("📉 SMART: fallos=%u/%u en %lu ms",
               failCount, maxFailRetries, (unsigned long)(millis()-failWindowStart));
      if (failCount >= maxFailRetries) {
        AWM_LOGW("🚪 SMART: abriendo portal por fallos acumulados");
        startPortal();
        failCount = 0; failWindowStart = 0;
      }
    }
  }
}

bool AyresWiFiManager::scanRedDetectada() {
  unsigned long ahora = millis();
  if (ahora - ultimoScan < SCAN_INTERVAL_MS) return false;
  ultimoScan = ahora;

  if (WiFi.status() == WL_CONNECTED && !portalActive) return false;

  int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);
  bool encontrada = false;
  for (int i = 0; i < n; ++i) {
    if (WiFi.SSID(i) == ssid) { encontrada = true; break; }
  }
  WiFi.scanDelete();
  return encontrada;
}

void AyresWiFiManager::forzarReconexion() {
  AWM_LOGI("🔄  Forzando reconexión…");
  // [CHANGED] Respetar AP externo para no tumbarlo
  if (portalActive || externalApActive) WiFi.mode(WIFI_AP_STA);
  else                                  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid.c_str(), password.c_str());
  ultimoIntentoWiFi = millis();
}

// =====================================================
//                     NTP / TIEMPO
// =====================================================
void AyresWiFiManager::sincronizarHoraNTP() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  for (int j = 0; j < 20; j++) {
    time_t now = time(nullptr);
    if (now > 100000) {
      AWM_LOGI("🕒 Hora sincronizada: %s", ctime(&now));
      return;
    }
    delay(200);
  }
  AWM_LOGW("⚠️ NTP no respondió. Continuando sin sincronizar.");
}

uint64_t AyresWiFiManager::getTimestamp() {
  time_t now = time(nullptr);
  return (now > 100000) ? static_cast<uint64_t>(now) * 1000ULL : 0;
}

// =====================================================
//                  INTERNET CHECK
// =====================================================
bool AyresWiFiManager::hayInternet() {
  if (WiFi.status() != WL_CONNECTED) return false;
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

// =====================================================
//                     LED FSM
// =====================================================
void AyresWiFiManager::setLedAuto(bool enable){
  ledAuto = enable;
  if (ledAuto) ledSet(LedPattern::OFF);
}
void AyresWiFiManager::setLedPatternManual(LedPattern p){
  ledAuto = false;
  ledSet(p);
}
void AyresWiFiManager::ledSet(LedPattern p){
  ledPat = p; ledStep = 0; ledT0 = millis();
}
void AyresWiFiManager::ledAutoUpdate(){
  if (!ledAuto) return;

  LedPattern want = LedPattern::OFF;

  // prioridad: escaneo > portal > conectado > idle
  if (scanning || (millis() < scanningUntil)) {
    want = LedPattern::BLINK_FAST;
  } else if (portalActive) {
    want = LedPattern::BLINK_SLOW;
  } else if (WiFi.status() == WL_CONNECTED) {
    want = LedPattern::ON;
  } else {
    want = LedPattern::OFF;
  }

  if (want != ledPat) ledSet(want);
}

void AyresWiFiManager::ledTask(){
  const unsigned long now = millis();

  auto write = [&](uint8_t v){
    if (ledOut != v){
      ledOut = v;
      digitalWrite(ledPin, v);
    }
  };

  switch (ledPat){
    case LedPattern::OFF: write(LOW); break;
    case LedPattern::ON:  write(HIGH); break;

    case LedPattern::BLINK_SLOW: {          // 500ms ON / 500ms OFF
      const unsigned long period = 1000;
      write( ((now - ledT0) % period) < 500 ? HIGH : LOW );
    } break;

    case LedPattern::BLINK_FAST: {          // 100ms ON / 100ms OFF
      const unsigned long period = 200;
      write( ((now - ledT0) % period) < 100 ? HIGH : LOW );
    } break;

    case LedPattern::BLINK_DOUBLE: {        // ON 120, OFF 120, ON 120, OFF 640
      static const uint16_t seq[] = {120,120,120,640};
      static const uint8_t  on [] = {1,  0,  1,  0  };
      if (now - ledT0 >= seq[ledStep]){
        ledT0 = now;
        ledStep = (ledStep + 1) % 4;
        write(on[ledStep] ? HIGH : LOW);
      }
    } break;

    case LedPattern::BLINK_TRIPLE: {        // ON 100, OFF 100 x3, OFF 500
      static const uint16_t seq[] = {100,100,100,100,100,500};
      static const uint8_t  on [] = {1,  0,  1,  0,  1,  0  };
      if (now - ledT0 >= seq[ledStep]){
        ledT0 = now;
        ledStep = (ledStep + 1) % 6;
        write(on[ledStep] ? HIGH : LOW);
      }
    } break;
  }
}

// =====================================================
//                  RECONNECT DRIVER
// =====================================================
void AyresWiFiManager::setAutoReconnect(bool habilitado) {
  autoReconnect = habilitado;
  WiFi.setAutoReconnect(habilitado);
}

// =====================================================
//             Helpers estáticos (borrado de JSONs)
// =====================================================
void AyresWiFiManager::setProtectedJsons(std::initializer_list<const char*> names) {
  _protectedExact.clear();
  for (auto n : names) {
    String s(n ? n : "");
    if (!s.length()) continue;
    if (!s.startsWith("/")) s = "/" + s;
    _protectedExact.push_back(s);
  }
}

bool AyresWiFiManager::isProtectedJson(const String& name) const {
  String n = name;
  if (!n.startsWith("/")) n = "/" + n;

  for (const auto& ex : _protectedExact) {
    if (n.equalsIgnoreCase(ex)) return true;
  }
  return false;
}

#if defined(ESP32)
// Recursivo + cierra el File ANTES de borrar (evita "Has open FD")
void AyresWiFiManager::eraseJsonInDir(const char* dirPath) {
  if (!dirPath || !*dirPath) return;

  File dir = LittleFS.open(dirPath);
  if (!dir || !dir.isDirectory()) return;

  String base = dirPath;
  if (!base.endsWith("/")) base += "/";

  // Iteración segura: cerrar el File 'f' ANTES de borrar
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();             // puede venir sin '/' inicial
    String full = name;
    if (!full.startsWith("/")) full = base + full;

    const bool isDir = f.isDirectory();
    f.close();                          // ← CERRAR HANDLE ANTES DE SEGUIR

    if (isDir) {
      eraseJsonInDir(full.c_str());     // recursivo
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
#else   // ESP8266 (no recursivo)
void AyresWiFiManager::eraseJsonInDir(const char* dirPath) {
  if (!dirPath || !*dirPath) return;

  Dir d = LittleFS.openDir(dirPath);
  while (d.next()) {
    String name = d.fileName();         // típicamente sin '/' inicial
    String full = name;
    if (!full.startsWith("/")) full = String("/") + full;

    if (full.endsWith(".json") && !isProtectedJson(full)) {
      if (LittleFS.remove(full)) {
        AWM_LOGI("🗑️  Borrado: %s", full.c_str());
      } else {
        AWM_LOGW("⚠️  No se pudo borrar: %s", full.c_str());
      }
    }
  }
}
#endif
