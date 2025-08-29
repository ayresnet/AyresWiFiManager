// AWM_Logging.h
#pragma once
#include <Arduino.h>

/*
 * AyresWiFiManager — Logging helper
 *
 * Defines:
 *   AWM_ENABLE_LOG  : 0 = off, 1 = on                (default: 1)
 *   AWM_LOG_LEVEL   : 0..5  (E=1,W=2,I=3,D=4,V=5)    (default: 3 = INFO)
 *   AWM_LOG_TAG     : const char* tag                (default: "AWM")
 *
 * Uso:
 *   AWM_LOGE("error: %d", code);
 *   AWM_LOGW("warn...");
 *   AWM_LOGI("info...");
 *   AWM_LOGD("debug x=%d", x);
 *   AWM_LOGV("verbose...");
 */

#ifndef AWM_ENABLE_LOG
#  define AWM_ENABLE_LOG 1
#endif

#ifndef AWM_LOG_LEVEL
#  define AWM_LOG_LEVEL 3   // INFO
#endif

#ifndef AWM_LOG_TAG
#  define AWM_LOG_TAG "AWM"
#endif

// Niveles
#define AWM_L_ERROR   1
#define AWM_L_WARN    2
#define AWM_L_INFO    3
#define AWM_L_DEBUG   4
#define AWM_L_VERBOSE 5

#if AWM_ENABLE_LOG

  // Interno: printf con prefijo [TAG] N:
  #define AWM__PRINTF(_name, fmt, ...) do { \
    Serial.printf("[" AWM_LOG_TAG "] " _name ": " fmt "\n", ##__VA_ARGS__); \
  } while (0)

  // ERROR
  #if (AWM_LOG_LEVEL >= AWM_L_ERROR)
    #define AWM_LOGE(fmt, ...) AWM__PRINTF("E", fmt, ##__VA_ARGS__)
  #else
    #define AWM_LOGE(...) do{}while(0)
  #endif

  // WARN
  #if (AWM_LOG_LEVEL >= AWM_L_WARN)
    #define AWM_LOGW(fmt, ...) AWM__PRINTF("W", fmt, ##__VA_ARGS__)
  #else
    #define AWM_LOGW(...) do{}while(0)
  #endif

  // INFO
  #if (AWM_LOG_LEVEL >= AWM_L_INFO)
    #define AWM_LOGI(fmt, ...) AWM__PRINTF("I", fmt, ##__VA_ARGS__)
  #else
    #define AWM_LOGI(...) do{}while(0)
  #endif

  // DEBUG
  #if (AWM_LOG_LEVEL >= AWM_L_DEBUG)
    #define AWM_LOGD(fmt, ...) AWM__PRINTF("D", fmt, ##__VA_ARGS__)
  #else
    #define AWM_LOGD(...) do{}while(0)
  #endif

  // VERBOSE
  #if (AWM_LOG_LEVEL >= AWM_L_VERBOSE)
    #define AWM_LOGV(fmt, ...) AWM__PRINTF("V", fmt, ##__VA_ARGS__)
  #else
    #define AWM_LOGV(...) do{}while(0)
  #endif

#else
  // Logs deshabilitados
  #define AWM_LOGE(...) do{}while(0)
  #define AWM_LOGW(...) do{}while(0)
  #define AWM_LOGI(...) do{}while(0)
  #define AWM_LOGD(...) do{}while(0)
  #define AWM_LOGV(...) do{}while(0)
#endif
