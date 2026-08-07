/*
 *  iAlarma — Firmware de central de alarma ESP32 (Firebase RTDB + AP offline)
 *  -------------------------------------------------------------------------
 *  Archivo   : AyresLog.h
 *  Versión   : 1.2.0
 *  Autor     : Daniel C. Salgado
 *  Empresa   : AyresNet IoT Systems
 *
 *  © 2025 AyresNet IoT Systems. Todos los derechos reservados.
 *
 *  Descripción
 *  -------------------------------------------------------------------------
 *  Macros y utilidades para logging unificado.
 *  Soporta salida por Serial, persistencia en archivo y nivel de verbosidad.
 *
 *  Documentación Técnica
 *  -------------------------------------------------------------------------
 *  Resumen
 *  ---------------------------------------------------------------
 *  Logging portable con niveles, prefijos y helpers para Arduino/PlatformIO.
 *  Emisión "single-shot" (arma la línea entera y escribe una sola vez) para
 *  reducir interleaving sin locks. Opcional: timestamp (ms/us), colores ANSI,
 *  CRLF, gating por nivel en runtime, y backend ESP_LOG en ESP32.
 *
 *  Soporte opcional de cadenas en FLASH (macros *_P): útil en AVR/ESP8266.
 *  En ESP32 no es necesario para ahorro de RAM, pero las macros *_P funcionan
 *  igual (caen a vsnprintf normal si no hay PROGMEM real).
 *
 *
 *  Características clave
 *  ---------------------------------------------------------------
 *  • 5 niveles: ERROR, WARN, INFO, DEBUG, VERBOSE
 *  • Prefijos configurables: [timestamp][TAG] Nivel: mensaje
 *  • Timestamp en ms o us (ESP32) opcional
 *  • Emisión single-shot sin necesidad de locks
 *  • Colores ANSI opcionales
 *  • Nivel de log en runtime opcional
 *  • Backend ESP_LOG (ESP-IDF) opcional
 *  • Soporte *_P (PROGMEM) opcional y portable
 *  • Alias de compatibilidad con AWM_Logging (AWM_LOG*)
 *
 *
 *  Configuración por macros (build_flags o antes del include)
 *  ---------------------------------------------------------------
 *  • AYLOG_ENABLED         : 0/1  — Habilita/deshabilita todo el log (default
 * 1) • AYLOG_LEVEL           : 1..5 — Nivel compile-time (E=1,W=2,I=3,D=4,V=5;
 * default 3=INFO) • AYLOG_TAG             : "TAG" — Texto del tag (default
 * "LOG") • AYLOG_PORT            : Print/Stream — Puerto de salida (default
 * Serial) • AYLOG_BUF_SZ          : int  — Tamaño buffer de mensaje (default
 * 256) • AYLOG_LINE_BUF_SZ     : int  — Tamaño buffer de línea (default
 * AYLOG_BUF_SZ+64) • AYLOG_SHOW_TS         : 0/1  — Prefijo con timestamp
 * (default 0) • AYLOG_TS_MICROS       : 0/1  — Timestamp en µs (ESP32) en vez
 * de ms (default 0) • AYLOG_COLOR           : 0/1  — Colores ANSI (default 0)
 *  • AYLOG_EOL_CRLF        : 0/1  — Fin de línea "\r\n" (default 0 = "\n")
 *  • AYLOG_RUNTIME_LEVEL   : 0/1  — Gating de nivel en runtime (default 0)
 *  • AYLOG_TRUNCATE_MARK   : "..."— Marcador de truncado (default "...")
 *  • AYLOG_LOCK / UNLOCK   : macros — Ganchos opcionales de exclusión (default
 * no-op) • AYLOG_BACKEND_ESP     : 0/1  — Redirige a ESP_LOG* (ESP32) (default
 * 0) • AYLOG_ENABLE_P_MACROS : 0/1  — Habilita AYLOG_*_P (default 0) •
 * AYLOG_P_SUPPORTED     : 0/1  — Fuerza soporte PROGMEM. Si NO está definido,
 * se autodefine: 1 en __AVR__/ESP8266/ESP32, 0 en otros targets. Podés forzarlo
 * a 0 para que *_P mapee a macros normales.
 *
 *
 *  Modos recomendados (recetas rápidas)
 *  ---------------------------------------------------------------
 *  A) ESP32 (simple y eficiente, sin *_P):
 *     build_flags =
 *       -DAYLOG_ENABLED=1
 *       -DAYLOG_LEVEL=4              ; DEBUG
 *       -DAYLOG_TAG=\"iAlarma\"
 *       -DAYLOG_SHOW_TS=1
 *       -DAYLOG_TS_MICROS=1
 *       -DAYLOG_RUNTIME_LEVEL=1
 *
 *     Uso:
 *       AYLOG_I("Inicio %s", PROJECT_VERSION);
 *
 *  B) Portabilidad AVR/ESP8266 + ESP32 (usar *_P):
 *     build_flags =
 *       -DAYLOG_ENABLED=1
 *       -DAYLOG_LEVEL=4
 *       -DAYLOG_TAG=\"iAlarma\"
 *       -DAYLOG_ENABLE_P_MACROS=1
 *
 *     Uso:
 *       // En AVR/ESP8266: PSTR() ahorra RAM real
 *       AYLOG_I_P(PSTR("Inicio %s"), PROJECT_VERSION);
 *       // En ESP32: también compila; PSTR pasa a const char*
 *
 *     Alternativa rápida si querés compilar YA en ESP32 con *_P:
 *       -DAYLOG_ENABLE_P_MACROS=1
 *       -DAYLOG_P_SUPPORTED=0        ; fuerza que *_P caiga a macros normales
 *
 *  C) Backend ESP_LOG (ESP32, integración IDF):
 *     build_flags =
 *       -DAYLOG_ENABLED=1
 *       -DAYLOG_LEVEL=4
 *       -DAYLOG_TAG=\"iAlarma\"
 *       -DAYLOG_BACKEND_ESP=1
 *       -DAYLOG_RUNTIME_LEVEL=0      ; gating propio de ESP_LOG
 *       -DAYLOG_SHOW_TS=0            ; el IDF maneja su timestamp
 *       -DAYLOG_ENABLE_P_MACROS=0
 *
 *     Uso:
 *       AYLOG_I("Conectado a WiFi");
 *
 *
 *  Notas técnicas
 *  ---------------------------------------------------------------
 *  • ISR safe: la librería detecta ISR en ESP32 con xPortInIsrContext()
 * (FreeRTOS) y omite locks en ese contexto. No redeclarar esa función en user
 * code. • Emisión single-shot: formatea la línea completa en memoria y la
 * escribe en 1 llamada. • Locks opcionales: si necesitás exclusión, redefiní
 * AYLOG_LOCK/UNLOCK. • Colores ANSI: útiles en terminales que los soporten;
 * desactivá en monitores simples. • Buffers: si tu mensaje puede ser largo,
 * aumentá AYLOG_BUF_SZ y/o AYLOG_LINE_BUF_SZ.
 *
 *
 *  Errores comunes y cómo resolverlos
 *  ---------------------------------------------------------------
 *  • "conflicting declaration of xPortInIsrContext" (ESP32):
 *      No declares esa función. Dejá que AyresLog incluya FreeRTOS y úsala tal
 * cual.
 *
 *  • "va_start used in function with fixed args" usando *_P:
 *      Solución integrada: las *_P usan una función variádica interna.
 *      Alternativa de build: -DAYLOG_P_SUPPORTED=0 (mapea *_P a macros
 * normales).
 *
 *  • Mezclar F("...") con *_P:
 *      Evitá F() con *_P; usá PSTR("...") o las macros sin _P.
 *
 *  Historial
 *  ---------------------------------------------------------------
 *  • 2025-09-29: v1.2.3 — Emisión single-shot, includes condicionales, backend
 * ESP32 seguro, mejoras _P cross-target, helpers ISR-safe, docs (actualizadas).
 *  • 2025-09-28: v1.2.2 — Soporte _P opcional y portable para strings en FLASH.
 *  • 2025-09-28: v1.2.1 — Correcciones: nivel runtime global, portabilidad %lu,
 *                         locks configurables, seguridad en truncado.
 *  • 2025-09-27: v1.2.0 — Timestamp ms/us, colores ANSI, CRLF, truncado
 * elegante, nivel en runtime y ganchos de lock; backend ESP_LOG opcional.
 */

#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ===== Includes condicionales para features opcionales ===== */
#if defined(ESP32) && defined(ARDUINO) && AYLOG_SHOW_TS && AYLOG_TS_MICROS
#include <esp_timer.h>
#endif

/* ==================== Detección de soporte para PROGMEM ====================
 */
#if !defined(AYLOG_P_SUPPORTED)
#if defined(__AVR__) || defined(ESP8266) || defined(ESP32)
#define AYLOG_P_SUPPORTED 1
#else
#define AYLOG_P_SUPPORTED 0
#endif
#endif

#ifndef AYLOG_ENABLE_P_MACROS
#define AYLOG_ENABLE_P_MACROS 0
#endif

/* ==================== Config (override por build_flags o antes del include)
 * ==================== */
#ifndef AYLOG_ENABLED
#define AYLOG_ENABLED 1
#endif
#ifndef AYLOG_LEVEL
#define AYLOG_LEVEL 3
#endif
#ifndef AYLOG_TAG
#define AYLOG_TAG "LOG"
#endif
#ifndef AYLOG_PORT
#define AYLOG_PORT Serial
#endif
#ifndef AYLOG_BUF_SZ
#define AYLOG_BUF_SZ 256
#endif
#ifndef AYLOG_LINE_BUF_SZ
#define AYLOG_LINE_BUF_SZ (AYLOG_BUF_SZ + 64)
#endif
#ifndef AYLOG_SHOW_TS
#define AYLOG_SHOW_TS 0
#endif
#ifndef AYLOG_TS_MICROS
#define AYLOG_TS_MICROS 0
#endif
#ifndef AYLOG_COLOR
#define AYLOG_COLOR 0
#endif
#ifndef AYLOG_EOL_CRLF
#define AYLOG_EOL_CRLF 0
#endif
#ifndef AYLOG_RUNTIME_LEVEL
#define AYLOG_RUNTIME_LEVEL 0
#endif
#ifndef AYLOG_TRUNCATE_MARK
#define AYLOG_TRUNCATE_MARK "..."
#endif

/* ==================== Locks y contexto ISR ==================== */
#if !defined(AYLOG_LOCK)
// Por defecto: no bloquear (single-shot ayuda a evitar interleaving).
#define AYLOG_LOCK()                                                           \
  do {                                                                         \
  } while (0)
#define AYLOG_UNLOCK()                                                         \
  do {                                                                         \
  } while (0)
#endif

#if defined(ESP32)
// Usar la firma oficial de FreeRTOS (no redeclarar).
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#define AYLOG_IN_ISR() (xPortInIsrContext() != 0)
#else
#define AYLOG_IN_ISR() (false)
#endif

/* ==================== Compat con defines antiguos de AWM_Logging
 * ==================== */
#ifdef AWM_ENABLE_LOG
#ifndef AYLOG_ENABLED
#define AYLOG_ENABLED AWM_ENABLE_LOG
#endif
#endif
#ifdef AWM_LOG_LEVEL
#ifndef AYLOG_LEVEL
#define AYLOG_LEVEL AWM_LOG_LEVEL
#endif
#endif
#ifdef AWM_LOG_TAG
#ifndef AYLOG_TAG
#define AYLOG_TAG AWM_LOG_TAG
#endif
#endif

/* ==================== Niveles ==================== */
#define AYLOG_L_ERROR 1
#define AYLOG_L_WARN 2
#define AYLOG_L_INFO 3
#define AYLOG_L_DEBUG 4
#define AYLOG_L_VERBOSE 5

#if AYLOG_ENABLED

/* --------- (Opcional) Nivel runtime (global) --------- */
#if AYLOG_RUNTIME_LEVEL
static inline uint8_t &_aylog_get_runtime_level() {
  static uint8_t level = AYLOG_LEVEL;
  return level;
}
#define _aylog_runtime_level (_aylog_get_runtime_level())
#define AYLOG_SET_LEVEL_RUNTIME(lvl)                                           \
  do {                                                                         \
    _aylog_get_runtime_level() = (uint8_t)(lvl);                               \
  } while (0)
#define _AY_RT_OK(lvl) (_aylog_runtime_level >= (lvl))
#else
#define AYLOG_SET_LEVEL_RUNTIME(lvl)                                           \
  do {                                                                         \
  } while (0)
#define _AY_RT_OK(lvl) (true)
#endif

/* --------- Colores ANSI (opcionales) --------- */
#if AYLOG_COLOR
#define _AYC_RED "\x1b[31m"
#define _AYC_YEL "\x1b[33m"
#define _AYC_CYN "\x1b[36m"
#define _AYC_BLU "\x1b[34m"
#define _AYC_MGT "\x1b[35m"
#define _AYC_RST "\x1b[0m"
#else
#define _AYC_RED ""
#define _AYC_YEL ""
#define _AYC_CYN ""
#define _AYC_BLU ""
#define _AYC_MGT ""
#define _AYC_RST ""
#endif

/* --------- EOL --------- */
#if AYLOG_EOL_CRLF
#define _AY_EOL "\r\n"
#else
#define _AY_EOL "\n"
#endif

/* --------- Helpers de prefijo --------- */
static inline size_t _aylog_write_ts_(char *dst, size_t cap) {
#if AYLOG_SHOW_TS
#if defined(ESP32) && AYLOG_TS_MICROS
  uint32_t us = (uint32_t)(esp_timer_get_time() & 0xFFFFFFFFULL);
  return (size_t)snprintf(dst, cap, "[%luus] ", (unsigned long)us);
#else
  unsigned long ms = millis();
  return (size_t)snprintf(dst, cap, "[%lums] ", ms);
#endif
#else
  (void)dst;
  (void)cap;
  return 0;
#endif
}

static inline void _aylog_truncate_inplace_(char *buf, size_t bufsz) {
  if (bufsz < 2)
    return;
  size_t mlen = strlen(AYLOG_TRUNCATE_MARK);
  if (mlen >= 3 && mlen < bufsz) {
    size_t pos = bufsz - 1 - mlen;
    memcpy(buf + pos, AYLOG_TRUNCATE_MARK, mlen);
    buf[bufsz - 1] = '\0';
  } else {
    if (bufsz >= 4) {
      buf[bufsz - 4] = '.';
      buf[bufsz - 3] = '.';
      buf[bufsz - 2] = '.';
      buf[bufsz - 1] = '\0';
    } else {
      buf[bufsz - 1] = '\0';
    }
  }
}

/* --------- Formateo del mensaje (RAM) --------- */
static inline void _aylog_format_msg_(char *out, size_t outsz, const char *fmt,
                                      va_list ap) {
  if (outsz < 2)
    return;
  int n = vsnprintf(out, outsz, fmt, ap);
  if (n < 0) {
    strncpy(out, "[fmtError]", outsz);
    out[outsz - 1] = '\0';
    return;
  }
  if ((size_t)n >= outsz)
    _aylog_truncate_inplace_(out, outsz);
}

/* --------- Formateo del mensaje (FLASH) --------- */
#if AYLOG_ENABLE_P_MACROS
#include <pgmspace.h>

// Si AYLOG_P_SUPPORTED no vino por build_flags, autodecidir (ya se definió
// arriba si faltaba)
static inline void _aylog_format_msg_P_(char *out, size_t outsz,
                                        const char *fmt_P, va_list ap) {
  if (outsz < 2)
    return;
#if AYLOG_P_SUPPORTED && (defined(__AVR__) || defined(ESP8266))
  // PROGMEM real → usar vsnprintf_P
  int n = vsnprintf_P(out, outsz, fmt_P, ap);
#else
  // ESP32 y otros → usar vsnprintf estándar
  int n = vsnprintf(out, outsz, fmt_P, ap);
#endif
  if (n < 0) {
    strncpy(out, "[fmtError]", outsz);
    out[outsz - 1] = '\0';
    return;
  }
  if ((size_t)n >= outsz)
    _aylog_truncate_inplace_(out, outsz);
}
#endif

/* --------- Emisión single-shot --------- */
static inline void _aylog_emit_line_(char lvlch, const char *msg) {
  char line[AYLOG_LINE_BUF_SZ];
  size_t pos = 0, cap = sizeof(line);

  // Timestamp
  pos += _aylog_write_ts_(line + pos, (pos < cap) ? (cap - pos) : 0);

  // [TAG] + nivel
  if (pos < cap)
    pos += (size_t)snprintf(line + pos, cap - pos, "[%s] ", AYLOG_TAG);
#if AYLOG_COLOR
  const char *col = (lvlch == 'E')   ? _AYC_RED
                    : (lvlch == 'W') ? _AYC_YEL
                    : (lvlch == 'I') ? _AYC_CYN
                    : (lvlch == 'D') ? _AYC_BLU
                                     : _AYC_MGT;
  if (pos < cap)
    pos += (size_t)snprintf(line + pos, cap - pos, "%s%c%s: ", col, lvlch,
                            _AYC_RST);
#else
  if (pos < cap)
    pos += (size_t)snprintf(line + pos, cap - pos, "%c: ", lvlch);
#endif

  // Mensaje
  if (pos < cap) {
    size_t rem = cap - pos;
    size_t mlen = strlen(msg);
    if (mlen < rem) {
      memcpy(line + pos, msg, mlen);
      pos += mlen;
    } else {
      // copiar lo que entra y marcar truncado en la línea
      memcpy(line + pos, msg, rem - 1);
      pos = cap - 1;
      line[pos] = '\0';
      _aylog_truncate_inplace_(line, cap);
      pos = strlen(line);
    }
  }

  // EOL
#if AYLOG_EOL_CRLF
  if (pos + 2 <= cap) {
    line[pos++] = '\r';
    line[pos++] = '\n';
  }
#else
  if (pos + 1 <= cap) {
    line[pos++] = '\n';
  }
#endif

  // Emitir (sin locks si ISR)
  if (!AYLOG_IN_ISR()) {
    AYLOG_LOCK();
  }
  AYLOG_PORT.write((const uint8_t *)line, pos);
  if (!AYLOG_IN_ISR()) {
    AYLOG_UNLOCK();
  }
}

/* --------- Macros públicas (RAM) --------- */
#define AYLOG__EMIT(_lvlch, _lvlnum, fmt, ...)                                 \
  do {                                                                         \
    if ((AYLOG_LEVEL >= (_lvlnum)) && _AY_RT_OK(_lvlnum)) {                    \
      char _msg[AYLOG_BUF_SZ];                                                 \
      int _n = snprintf(_msg, sizeof(_msg), fmt, ##__VA_ARGS__);               \
      if (_n < 0) {                                                            \
        strncpy(_msg, "[fmtError]", sizeof(_msg));                             \
        _msg[sizeof(_msg) - 1] = '\0';                                         \
      } else if ((size_t)_n >= sizeof(_msg)) {                                 \
        _aylog_truncate_inplace_(_msg, sizeof(_msg));                          \
      }                                                                        \
      _aylog_emit_line_(_lvlch, _msg);                                         \
    }                                                                          \
  } while (0)

#define AYLOG_E(fmt, ...) AYLOG__EMIT('E', AYLOG_L_ERROR, fmt, ##__VA_ARGS__)
#define AYLOG_W(fmt, ...) AYLOG__EMIT('W', AYLOG_L_WARN, fmt, ##__VA_ARGS__)
#define AYLOG_I(fmt, ...) AYLOG__EMIT('I', AYLOG_L_INFO, fmt, ##__VA_ARGS__)
#define AYLOG_D(fmt, ...) AYLOG__EMIT('D', AYLOG_L_DEBUG, fmt, ##__VA_ARGS__)
#define AYLOG_V(fmt, ...) AYLOG__EMIT('V', AYLOG_L_VERBOSE, fmt, ##__VA_ARGS__)

/* --------- Macros públicas (_P) --------- */
#if AYLOG_ENABLE_P_MACROS
#if AYLOG_P_SUPPORTED
// Variádica estilo printf_P: evita "va_start en función con args fijos".
static inline void _aylog_emit_line_fmtP_(char lvlch, uint8_t lvlnum,
                                          const char *fmt_P, ...) {
  if (!((AYLOG_LEVEL >= lvlnum) && _AY_RT_OK(lvlnum)))
    return;
  char _msg[AYLOG_BUF_SZ];
  va_list _ap;
  va_start(_ap, fmt_P);
  _aylog_format_msg_P_(_msg, sizeof(_msg), fmt_P, _ap);
  va_end(_ap);
  _aylog_emit_line_(lvlch, _msg);
}
#define AYLOG_E_P(fmt_P, ...)                                                  \
  _aylog_emit_line_fmtP_('E', AYLOG_L_ERROR, fmt_P, ##__VA_ARGS__)
#define AYLOG_W_P(fmt_P, ...)                                                  \
  _aylog_emit_line_fmtP_('W', AYLOG_L_WARN, fmt_P, ##__VA_ARGS__)
#define AYLOG_I_P(fmt_P, ...)                                                  \
  _aylog_emit_line_fmtP_('I', AYLOG_L_INFO, fmt_P, ##__VA_ARGS__)
#define AYLOG_D_P(fmt_P, ...)                                                  \
  _aylog_emit_line_fmtP_('D', AYLOG_L_DEBUG, fmt_P, ##__VA_ARGS__)
#define AYLOG_V_P(fmt_P, ...)                                                  \
  _aylog_emit_line_fmtP_('V', AYLOG_L_VERBOSE, fmt_P, ##__VA_ARGS__)
#else
// Soporte _P desactivado por build → mapear a RAM (compatibilidad inmediata)
#define AYLOG_E_P(fmt_P, ...) AYLOG_E(fmt_P, ##__VA_ARGS__)
#define AYLOG_W_P(fmt_P, ...) AYLOG_W(fmt_P, ##__VA_ARGS__)
#define AYLOG_I_P(fmt_P, ...) AYLOG_I(fmt_P, ##__VA_ARGS__)
#define AYLOG_D_P(fmt_P, ...) AYLOG_D(fmt_P, ##__VA_ARGS__)
#define AYLOG_V_P(fmt_P, ...) AYLOG_V(fmt_P, ##__VA_ARGS__)
#endif
#endif

#else /* AYLOG_ENABLED == 0 */

#define AYLOG_E(...)                                                           \
  do {                                                                         \
  } while (0)
#define AYLOG_W(...)                                                           \
  do {                                                                         \
  } while (0)
#define AYLOG_I(...)                                                           \
  do {                                                                         \
  } while (0)
#define AYLOG_D(...)                                                           \
  do {                                                                         \
  } while (0)
#define AYLOG_V(...)                                                           \
  do {                                                                         \
  } while (0)
#define AYLOG_SET_LEVEL_RUNTIME(lvl)                                           \
  do {                                                                         \
  } while (0)

#if AYLOG_ENABLE_P_MACROS
#define AYLOG_E_P(...)                                                         \
  do {                                                                         \
  } while (0)
#define AYLOG_W_P(...)                                                         \
  do {                                                                         \
  } while (0)
#define AYLOG_I_P(...)                                                         \
  do {                                                                         \
  } while (0)
#define AYLOG_D_P(...)                                                         \
  do {                                                                         \
  } while (0)
#define AYLOG_V_P(...)                                                         \
  do {                                                                         \
  } while (0)
#endif

#endif /* AYLOG_ENABLED */

/* ==================== Alias para código viejo (AWM_Logging)
 * ==================== */
#ifndef AWM_L_ERROR
#define AWM_L_ERROR AYLOG_L_ERROR
#define AWM_L_WARN AYLOG_L_WARN
#define AWM_L_INFO AYLOG_L_INFO
#define AWM_L_DEBUG AYLOG_L_DEBUG
#define AWM_L_VERBOSE AYLOG_L_VERBOSE
#endif

#ifndef AWM_LOGE
#define AWM_LOGE AYLOG_E
#define AWM_LOGW AYLOG_W
#define AWM_LOGI AYLOG_I
#define AWM_LOGD AYLOG_D
#define AWM_LOGV AYLOG_V
#endif

#if AYLOG_ENABLE_P_MACROS
#ifndef AWM_LOGE_P
#define AWM_LOGE_P AYLOG_E_P
#define AWM_LOGW_P AYLOG_W_P
#define AWM_LOGI_P AYLOG_I_P
#define AWM_LOGD_P AYLOG_D_P
#define AWM_LOGV_P AYLOG_V_P
#endif
#endif

/* ==================== Backend ESP_LOG opcional (solo ESP32)
 * ==================== */
#if defined(ESP32) && defined(AYLOG_BACKEND_ESP)
#include <esp_log.h>
#undef AYLOG_E
#undef AYLOG_W
#undef AYLOG_I
#undef AYLOG_D
#undef AYLOG_V
#define AYLOG_E(fmt, ...) ESP_LOGE(AYLOG_TAG, fmt, ##__VA_ARGS__)
#define AYLOG_W(fmt, ...) ESP_LOGW(AYLOG_TAG, fmt, ##__VA_ARGS__)
#define AYLOG_I(fmt, ...) ESP_LOGI(AYLOG_TAG, fmt, ##__VA_ARGS__)
#define AYLOG_D(fmt, ...) ESP_LOGD(AYLOG_TAG, fmt, ##__VA_ARGS__)
#define AYLOG_V(fmt, ...) ESP_LOGV(AYLOG_TAG, fmt, ##__VA_ARGS__)
/* Si usan *_P con backend ESP_LOG, mapeamos también a las normales para
 * unificar */
#if AYLOG_ENABLE_P_MACROS
#undef AYLOG_E_P
#undef AYLOG_W_P
#undef AYLOG_I_P
#undef AYLOG_D_P
#undef AYLOG_V_P
#define AYLOG_E_P(fmt_P, ...) AYLOG_E(fmt_P, ##__VA_ARGS__)
#define AYLOG_W_P(fmt_P, ...) AYLOG_W(fmt_P, ##__VA_ARGS__)
#define AYLOG_I_P(fmt_P, ...) AYLOG_I(fmt_P, ##__VA_ARGS__)
#define AYLOG_D_P(fmt_P, ...) AYLOG_D(fmt_P, ##__VA_ARGS__)
#define AYLOG_V_P(fmt_P, ...) AYLOG_V(fmt_P, ##__VA_ARGS__)
#endif
#endif
