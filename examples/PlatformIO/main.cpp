/*
 * PlatformIO example - AyresWiFiManager
 *
 * Copy this file to src/main.cpp in your PlatformIO project.
 */

#include <Arduino.h>
#include <AyresLog.h>
#include <AyresWiFiManager.h>

namespace {

// Run the Internet reachability check every 30 seconds.
constexpr uint32_t INTERNET_CHECK_INTERVAL_MS = 30000;
// Print connection diagnostics every 5 seconds.
constexpr uint32_t DIAGNOSTIC_INTERVAL_MS = 5000;

// The library manager owns Wi-Fi provisioning and reconnection state.
AyresWiFiManager wifiManager;

uint32_t lastInternetCheck = 0;
uint32_t lastDiagnostic = 0;
AyresWiFiManager::State previousState = AyresWiFiManager::State::OFFLINE;

void printDiagnostics() {
  // Read the current state and the last error reported by the manager.
  const AyresWiFiManager::State state = wifiManager.getState();
  const AyresWiFiManager::Error error = wifiManager.getLastError();

  AWM_LOGI("Estado=%s | Error=%s | Reconexiones=%lu | RSSI=%d dBm",
           AyresWiFiManager::stateToString(state),
           AyresWiFiManager::errorToString(error),
           static_cast<unsigned long>(wifiManager.getReconnectCount()),
           wifiManager.getSignalStrength());
}

} // namespace

void setup() {
  // Start the serial console used by AyresLog.
  Serial.begin(115200);
  delay(200);

  AWM_LOGI("AyresWiFiManager %s - ejemplo PlatformIO", AWM_VERSION);

  // Configure the device hostname and the temporary setup access point.
  const String macSuffix = AyresWiFiManager::getMacSuffix();
  wifiManager.setHostname(String("awm-test-") + macSuffix);
  wifiManager.setAPCredentials(String("AWM-Setup-") + macSuffix, "12345678");
  AWM_LOGI("MAC Station: %s", AyresWiFiManager::getMacAddress().c_str());
  // Enable the captive portal and stop waiting after five minutes.
  wifiManager.setCaptivePortal(true);
  wifiManager.setPortalTimeout(300);
  // Enable checks for connected clients and web reachability.
  wifiManager.setAPClientCheck(true);
  wifiManager.setWebClientCheck(true);

  // Retry intelligently before falling back to the setup portal.
  wifiManager.setFallbackPolicy(
      AyresWiFiManager::FallbackPolicy::SMART_RETRIES);
  wifiManager.setSmartRetries(3, 60000);
  // Add delays between reconnect attempts and enable automatic recovery.
  wifiManager.setReconnectBackoffMs(10000);
  wifiManager.setReconnectAttemptMs(7000);
  wifiManager.setAutoReconnect(true);
  // A long press on the configured button opens or resets the portal.
  wifiManager.enableButtonPortal(true);

  // true encrypts /wifi.json; false stores it as plain text.
  wifiManager.setCredentialEncryption(true, "AyresNet2020WiFi");

  // Load credentials, initialize Wi-Fi, and start the manager.
  wifiManager.begin();
  wifiManager.run();

  previousState = wifiManager.getState();
  printDiagnostics();
  AWM_LOGI("Boton: 2-5 s abre el portal; 5 s o mas borra credenciales");
}

void loop() {
  // Keep the manager state machine and reconnect logic running.
  wifiManager.update();
  wifiManager.reintentarConexionSiNecesario();

  const uint32_t now = millis();
  const AyresWiFiManager::State currentState = wifiManager.getState();

  // Report state transitions as soon as they occur.
  if (currentState != previousState) {
    AWM_LOGI("Cambio de estado: %s -> %s",
             AyresWiFiManager::stateToString(previousState),
             AyresWiFiManager::stateToString(currentState));
    previousState = currentState;
  }

  // Check Internet access periodically while Wi-Fi is connected.
  if (wifiManager.isConnected() &&
      now - lastInternetCheck >= INTERNET_CHECK_INTERVAL_MS) {
    lastInternetCheck = now;
    wifiManager.hayInternet();
  }

  // Emit periodic diagnostics for monitoring and troubleshooting.
  if (now - lastDiagnostic >= DIAGNOSTIC_INTERVAL_MS) {
    lastDiagnostic = now;
    printDiagnostics();
  }

  delay(2);
}
