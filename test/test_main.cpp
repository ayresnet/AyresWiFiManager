/**
 * @file test_main.cpp
 * @brief Tests principales de AyresWiFiManager
 *
 * Combina todos los tests en un solo archivo para evitar conflictos de setup()
 */

#include "AyresWiFiManager.h"
#include <unity.h>

// ============== TESTS DE VERSIÓN ==============

void test_version_string() {
  const char *version = AyresWiFiManager::versionString();
  TEST_ASSERT_NOT_NULL(version);
  TEST_ASSERT_EQUAL_STRING("2.2.1", version);
}

void test_version_code() {
  uint32_t code = AyresWiFiManager::versionCode();
  // v2.2.1 formula: (MAJOR << 16) | (MINOR << 8) | PATCH
  // (2 << 16) | (2 << 8) | 1 = 131072 + 512 + 1 = 131585
  TEST_ASSERT_EQUAL_UINT32(131585, code);
}

void test_version_macros() {
  TEST_ASSERT_EQUAL(2, AWM_VERSION_MAJOR);
  TEST_ASSERT_EQUAL(2, AWM_VERSION_MINOR);
  TEST_ASSERT_EQUAL(1, AWM_VERSION_PATCH);
}

// ============== TESTS DE CONFIGURACIÓN ==============

void test_default_constructor() {
  AyresWiFiManager wifi;
  TEST_PASS_MESSAGE("Constructor ejecutado correctamente");
}

void test_set_ap_credentials() {
  AyresWiFiManager wifi;
  wifi.setAPCredentials("TestSSID", "TestPass123");
  TEST_PASS_MESSAGE("AP credentials set successfully");
}

void test_set_hostname() {
  AyresWiFiManager wifi;
  wifi.setHostname("test-device");
  TEST_PASS_MESSAGE("Hostname set successfully");
}

void test_set_portal_timeout() {
  AyresWiFiManager wifi;
  wifi.setPortalTimeout(300);
  TEST_PASS_MESSAGE("Portal timeout set successfully");
}

void test_portal_not_active_initially() {
  AyresWiFiManager wifi;
  bool isActive = wifi.isPortalActive();
  TEST_ASSERT_FALSE(isActive);
}

// ============== SETUP & LOOP ==============

void setup() {
  delay(2000);

  UNITY_BEGIN();

  // Tests de versión
  RUN_TEST(test_version_string);
  RUN_TEST(test_version_code);
  RUN_TEST(test_version_macros);

  // Tests de configuración
  RUN_TEST(test_default_constructor);
  RUN_TEST(test_set_ap_credentials);
  RUN_TEST(test_set_hostname);
  RUN_TEST(test_set_portal_timeout);
  RUN_TEST(test_portal_not_active_initially);

  UNITY_END();
}

void loop() {
  // Los tests solo corren una vez en setup()
}
