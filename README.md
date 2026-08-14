# AyresWiFiManager

[![Version](https://img.shields.io/badge/version-2.3.0-4361ee)](https://github.com/ayresnet/AyresWiFiManager)
[![Platform](https://img.shields.io/badge/platform-ESP32-2ec27e?logo=espressif)](https://www.espressif.com/en/products/socs/esp32)
[![Arduino](https://img.shields.io/badge/framework-Arduino-00979d?logo=arduino)](https://www.arduino.cc/)
[![License](https://img.shields.io/badge/license-MIT-6c757d)](LICENSE)

AyresWiFiManager (AWM) is an ESP32 library for Wi-Fi provisioning and connectivity management. It combines a captive portal, LittleFS credential storage, explicit fallback policies, reconnection control, NTP synchronization and field diagnostics behind a small Arduino-friendly API.

[Leer en español](README.es.md)

## Why AWM

- Captive portal with SoftAP, DNS catch-all and operating-system detection routes.
- Responsive portal UI served from LittleFS, with embedded GZIP pages as fallback.
- Wi-Fi scanning, credential provisioning and configurable portal inactivity timeout.
- Explicit fallback policies: `ON_FAIL`, `NO_CREDENTIALS_ONLY`, `SMART_RETRIES`, `BUTTON_ONLY` and `NEVER`.
- Non-blocking reconnection driver with configurable backoff and attempt windows.
- Unified connectivity state and diagnostic information for application code.
- Automatic LED patterns and boot-button actions.
- Optional credential encryption at rest.
- Optional compile-time logging.

## Compatibility

Version 2.3.0 officially supports:

- ESP32 using the Arduino framework.
- ArduinoJson 6.21.2 or newer within major version 6.
- LittleFS, DNSServer, WebServer and HTTPClient from the ESP32 Arduino core.

Version 2.3.0 supports ESP32 exclusively.

## Installation

### Arduino IDE

Install `AyresWiFiManager` from Library Manager and ensure ArduinoJson 6 is available. The portal has embedded fallback pages, so a LittleFS upload is optional unless you customize the UI.

### PlatformIO

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
board_build.filesystem = littlefs

lib_deps =
  ayresnet/AyresWiFiManager@^2.3.0
```

## Quick start

```cpp
#include <AyresWiFiManager.h>

AyresWiFiManager wifi;

void setup() {
  Serial.begin(115200);

  wifi.setHostname("my-device");
  wifi.setAPCredentials("MyDevice-Setup", "change-me");
  wifi.setPortalTimeout(300);
  wifi.setAPClientCheck(true);
  wifi.setWebClientCheck(true);
  wifi.setFallbackPolicy(
      AyresWiFiManager::FallbackPolicy::SMART_RETRIES);
  wifi.setSmartRetries(3, 60000);

  wifi.begin();
  wifi.run();
}

void loop() {
  wifi.update();
  wifi.reintentarConexionSiNecesario();
}
```

Use an empty AP password for an open provisioning network, or at least eight characters for a protected SoftAP.

## Connectivity state and diagnostics

AWM exposes one primary state so applications do not need to combine several low-level checks:

```cpp
AyresWiFiManager::State state = wifi.getState();

Serial.println(AyresWiFiManager::stateToString(state));
Serial.println(AyresWiFiManager::errorToString(wifi.getLastError()));
Serial.println(wifi.getReconnectCount());
Serial.println(wifi.getLastInternetCheck());
```

Available states:

- `OFFLINE`
- `WIFI_CONNECTING`
- `WIFI_CONNECTED`
- `INTERNET_OK`
- `NO_INTERNET`
- `PORTAL_ACTIVE`

`PORTAL_ACTIVE` takes precedence while the captive portal is open, including AP+STA operation. `INTERNET_OK` and `NO_INTERNET` are updated when `hayInternet()` runs. `getLastInternetCheck()` returns the last check time in milliseconds since boot.

## Lifecycle

- `begin()` initializes GPIO, Wi-Fi and LittleFS, then loads stored credentials.
- `run()` handles the boot-button window, performs the initial connection and applies the selected fallback policy.
- `update()` serves HTTP and DNS requests, updates LED patterns and handles portal timeouts. Call it on every loop iteration.
- `reintentarConexionSiNecesario()` advances the non-blocking reconnection state machine.

## Connectivity checks, privacy and trust

`hayInternet()` is an **optional reachability probe**, not a security check. When called, it performs an HTTP request to `http://clients3.google.com/generate_204` and returns `true` only for an HTTP `204` response. This updates `INTERNET_OK` or `NO_INTERNET`; it does not send the configured Wi-Fi SSID, Wi-Fi password, encryption key, portal form data, or application payload.

After connecting, AWM also tries to synchronize time with NTP. If NTP is unavailable, it can use the public HTTP `Date` header from `http://google.com` or `http://worldtimeapi.org/api/ip` as a fallback. This fallback likewise does not transmit stored credentials.

Because those probes use plain HTTP, a captive network, proxy, or malicious network can forge, redirect, or modify their response. Therefore:

- Treat `hayInternet()` as a practical indication of network reachability, not proof that a connection is trusted or private.
- Do not make authorization, payment, firmware-validation, or other security-sensitive decisions solely from `INTERNET_OK`, `NO_INTERNET`, or the HTTP time fallback.
- Applications that exchange sensitive data must use their own HTTPS/TLS connection and validate the remote service as appropriate.

HTTP is intentional here: it keeps the reachability test lightweight and compatible with captive networks. HTTPS can provide stronger authenticity, but requires certificate handling and consumes additional flash/RAM on constrained devices.

## Captive portal

The portal uses these local endpoints:

| Method | Route | Purpose |
| --- | --- | --- |
| `GET` | `/` | Portal UI |
| `GET` | `/scan` or `/scan.json` | Nearby Wi-Fi networks |
| `GET` | `/info` | Library, AP and host information |
| `POST` | `/save` | Save credentials and restart |
| `POST` | `/erase` | Remove only `/wifi.json` or perform a confirmed JSON reset |

Captive-network detection routes for Android, iOS and Windows redirect to the portal when captive mode is enabled.

The recovery section offers two different operations. `scope=wifi` removes only `/wifi.json`. `scope=all` searches LittleFS recursively; protected files are honored unless `force=1` is explicitly included. The portal's full-reset action uses `force=1`, reports how many files were found, removed or failed, and restarts only when the operation completes without errors.

AWM closes every file handle it owns before deleting and retries each removal three times. It cannot safely close a handle owned by another library or application component; such files are reported as failed instead of claiming a successful reset.

Custom pages can be uploaded to LittleFS as `data/index.html`, `data/success.html` and `data/error.html`. Use `setHtmlPathPrefix()` when storing them under a subdirectory.

## Fallback policies

- `NO_CREDENTIALS_ONLY` — default; opens the portal only when no credentials exist.
- `ON_FAIL` — opens the portal after the initial connection fails.
- `SMART_RETRIES` — opens it after the configured number of reconnect failures.
- `BUTTON_ONLY` — only a physical or application action opens it.
- `NEVER` — disables automatic portal fallback.

## Button and LED

Default pins are GPIO 0 for the active-low button and GPIO 2 for the LED.

- Hold the button for 2–5 seconds to open the portal.
- Hold it for 5 seconds or longer to erase credentials and restart.
- Slow blink: disconnected from Wi-Fi.
- Fast blink: connecting or scanning.
- Solid: connected to Wi-Fi.
- Double blink: Wi-Fi connected without verified Internet access.
- Triple blink: captive portal active.

Pins can be changed with the constructor:

```cpp
AyresWiFiManager wifi(LED_PIN, BUTTON_PIN);
```

## Credential storage and encryption

Credentials are stored in `/wifi.json`. Select the storage mode before `begin()` with one boolean; the key must contain exactly 16 bytes when encryption is enabled:

```cpp
wifi.setCredentialEncryption(true, myPrivateKey);
wifi.begin();
```

Use `false` instead of `true` for plaintext storage. Changing the value migrates an existing file automatically in either direction, provided the same key is supplied. The method returns `false` and leaves encryption disabled when the key is invalid.

Do not commit a real key to a public repository. With encryption disabled, the file contains only `{"ssid":"...","password":"..."}`. With encryption enabled, the complete credential record is stored as one authenticated AES-128-GCM envelope represented by a single opaque JSON string, such as `"QVdNA..."`; it exposes no field names or algorithm metadata. Older encrypted objects and legacy CBC records are read and automatically migrated to this envelope. This still does not protect a device whose firmware and key can both be extracted; stronger threat models require hardware-backed storage.

AWM deliberately does not embed an AES key. The sketch using the library supplies its own 16-byte key; each project must decide how to provision and protect it. For public source repositories, keep the real key outside version control (for example, in a private build configuration or external provisioning process).

### Reusing credentials without exposing them

Applications that need the stored Wi-Fi password for another local access check do not need to retrieve or duplicate it:

```cpp
wifi.setAPCredentialsUsingStoredPassword("Device-Control");

if (wifi.verifyWiFiPassword(candidate)) {
  // Authorized.
}
```

`setAPCredentialsUsingStoredPassword()` configures the SoftAP inside AWM and returns `false` when the stored password is not valid for WPA (8 to 63 characters). `verifyWiFiPassword()` returns only a boolean and uses a comparison whose work does not depend on the first differing character. `getWiFiPass()` remains available for source compatibility, but new integrations should prefer these methods so the secret stays owned by AWM.

## Logging

AWM uses `AyresLog.h` internally and retains the `AWM_LOGE`, `AWM_LOGW`, `AWM_LOGI`, `AWM_LOGD` and `AWM_LOGV` macros for application diagnostics.

```ini
build_flags =
  -D AWM_ENABLE_LOG=1
  -D AWM_LOG_LEVEL=3
  -D AWM_LOG_TAG=\"AWM\"
```

Set `AWM_ENABLE_LOG=0` to compile logging out. Levels range from 1 (`ERROR`) to 5 (`VERBOSE`).

## Repository layout

```text
data/          Custom captive-portal pages
examples/      Arduino and PlatformIO examples
src/           Public headers and library implementation
.github/       Continuous integration
```

The local `src/main.cpp` development harness is intentionally ignored and is not distributed as part of the library.

## AyresNet GZIP Asset Compiler

The repository includes `ayres_gzip.py`, a dependency-free tool developed by AyresNet. It has no hardcoded input files or output directories. Running it without arguments opens an interactive wizard:

```bash
python ayres_gzip.py
```

The wizard accepts one or more files, folders, or glob patterns and asks where to create the `.h` or `.hpp` header. The same operations can be automated from the command line:

```bash
# One file
python ayres_gzip.py web/index.html -o include/web_gz.h

# Multiple files
python ayres_gzip.py web/index.html web/app.css web/app.js -o include/web_gz.h

# A complete directory, including subdirectories
python ayres_gzip.py web -r -o include/web_gz.h

# Selected file types and portable C++ output
python ayres_gzip.py assets -r -I "*.html" -I "*.css" -o include/assets_gz.hpp -f cpp -p MY_APP
```

To regenerate this library's embedded portal explicitly:

```bash
python ayres_gzip.py data/index.html data/success.html data/error.html -o src/AWM_html_gz.h
```

Use `--check` with the same arguments in CI to fail when a header is outdated. Generated GZIP streams omit timestamps and source filenames, producing stable Git diffs. Run `python ayres_gzip.py -h` or `python ayres_gzip.py --help` for all options.

## License

MIT © AyresNet. See [LICENSE](LICENSE).
