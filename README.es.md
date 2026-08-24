# AyresWiFiManager

[![Versión](https://img.shields.io/badge/versión-2.3.0-4361ee)](https://github.com/ayresnet/AyresWiFiManager)
[![Plataforma](https://img.shields.io/badge/plataforma-ESP32-2ec27e?logo=espressif)](https://www.espressif.com/en/products/socs/esp32)
[![Arduino](https://img.shields.io/badge/framework-Arduino-00979d?logo=arduino)](https://www.arduino.cc/)
[![Licencia](https://img.shields.io/badge/licencia-MIT-6c757d)](LICENSE)

AyresWiFiManager (AWM) es una librería para provisionar y administrar la conectividad Wi-Fi de un ESP32. Reúne un portal cautivo, credenciales en LittleFS, políticas de fallback, reconexión, NTP y diagnóstico de campo detrás de una API sencilla para Arduino.

[Read in English](README.md)

## Características

- Portal cautivo con SoftAP, DNS catch-all y rutas de detección de los sistemas operativos.
- Interfaz adaptable servida desde LittleFS, con páginas GZIP embebidas como respaldo.
- Escaneo de redes, provisión de credenciales y timeout de inactividad configurable.
- Políticas explícitas: `ON_FAIL`, `NO_CREDENTIALS_ONLY`, `SMART_RETRIES`, `BUTTON_ONLY` y `NEVER`.
- Reconexión no bloqueante con backoff y ventana de intento configurables.
- Estado único de conectividad y datos de diagnóstico para cualquier proyecto.
- Patrones automáticos de LED y acciones mediante botón físico.
- Cifrado opcional de credenciales en reposo.
- Logging opcional en tiempo de compilaciÃ³n.

## Compatibilidad

La versión 2.3.0 soporta oficialmente:

- ESP32 con framework Arduino.
- ArduinoJson 6.21.2 o posterior dentro de la versión mayor 6.
- LittleFS, DNSServer, WebServer y HTTPClient del core Arduino para ESP32.

La versión 2.3.0 funciona exclusivamente con ESP32.

## Instalación

### Arduino IDE

Instalá `AyresWiFiManager` desde el Library Manager y verificá que ArduinoJson 6 esté disponible. El portal incluye páginas embebidas, por lo que cargar LittleFS es opcional salvo que quieras personalizar la interfaz.

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

## Uso básico

```cpp
#include <AyresWiFiManager.h>

AyresWiFiManager wifi;

void setup() {
  Serial.begin(115200);

  wifi.setHostname("mi-dispositivo");
  wifi.setAPCredentials("MiEquipo-Setup", "cambiar-clave");
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

Usá una clave vacía para un AP abierto o una clave de ocho caracteres como mínimo para proteger la red de provisión.

Para generar un nombre de configuración único a partir de la MAC Station:

```cpp
const String macSuffix = AyresWiFiManager::getMacSuffix(); // por ejemplo, "4C25"
wifi.setHostname(String("mi-dispositivo-") + macSuffix);
wifi.setAPCredentials(String("MiEquipo-Setup-") + macSuffix, "cambiar-clave");

const String macCompleta = AyresWiFiManager::getMacAddress();
```

## Estado y diagnóstico

La aplicación puede consultar un único estado en vez de combinar varias funciones:

```cpp
AyresWiFiManager::State estado = wifi.getState();

Serial.println(AyresWiFiManager::stateToString(estado));
Serial.println(AyresWiFiManager::errorToString(wifi.getLastError()));
Serial.println(wifi.getReconnectCount());
Serial.println(wifi.getLastInternetCheck());
```

Estados disponibles:

- `OFFLINE`
- `WIFI_CONNECTING`
- `WIFI_CONNECTED`
- `INTERNET_OK`
- `NO_INTERNET`
- `PORTAL_ACTIVE`

`PORTAL_ACTIVE` tiene prioridad mientras el portal esté abierto, incluso en modo AP+STA. `INTERNET_OK` y `NO_INTERNET` se actualizan cuando se llama a `hayInternet()`. `getLastInternetCheck()` devuelve milisegundos desde el arranque.

## Ciclo de vida

- `begin()` inicializa GPIO, Wi-Fi y LittleFS, y carga las credenciales.
- `run()` procesa el botón durante el arranque, intenta conectar y aplica la política elegida.
- `update()` atiende HTTP y DNS, actualiza el LED y controla el timeout del portal. Debe ejecutarse en cada vuelta del loop.
- `reintentarConexionSiNecesario()` avanza la máquina de reconexión no bloqueante.

## Comprobación de conectividad, privacidad y confianza

`hayInternet()` es una **sonda opcional de alcance de red**, no una comprobación de seguridad. Cuando se la llama, realiza una petición HTTP a `http://clients3.google.com/generate_204` y devuelve `true` solamente si recibe HTTP `204`. Esto actualiza `INTERNET_OK` o `NO_INTERNET`; no envía el SSID configurado, la contraseña Wi-Fi, la clave de cifrado, los datos del formulario del portal ni datos de la aplicación.

Luego de conectarse, AWM también intenta sincronizar la hora mediante NTP. Si NTP no está disponible, puede usar como respaldo la cabecera pública HTTP `Date` de `http://google.com` o `http://worldtimeapi.org/api/ip`. Este respaldo tampoco transmite las credenciales guardadas.

Como esas comprobaciones usan HTTP sin cifrar, una red cautiva, un proxy o una red maliciosa puede falsificar, redirigir o modificar la respuesta. Por lo tanto:

- Considerá `hayInternet()` una indicación práctica de alcance de red, no una prueba de que la conexión sea confiable o privada.
- No tomes decisiones de autorización, pagos, validación de firmware u otras decisiones sensibles basándote únicamente en `INTERNET_OK`, `NO_INTERNET` o el respaldo horario por HTTP.
- Si tu aplicación intercambia datos sensibles, debe usar su propia conexión HTTPS/TLS y validar el servicio remoto según corresponda.

HTTP es intencional en este caso: mantiene la prueba de alcance liviana y compatible con redes cautivas. HTTPS puede aportar mayor autenticidad, pero requiere gestionar certificados y consume memoria flash/RAM adicional en dispositivos con recursos limitados.

## Portal cautivo

| Método | Ruta | Función |
| --- | --- | --- |
| `GET` | `/` | Interfaz del portal |
| `GET` | `/scan` o `/scan.json` | Redes Wi-Fi cercanas |
| `GET` | `/info` | Información de librería, AP y host |
| `POST` | `/save` | Guarda credenciales y reinicia |
| `POST` | `/erase` | Elimina solo `/wifi.json` o ejecuta un borrado JSON confirmado |

Las páginas personalizadas se guardan como `data/index.html`, `data/success.html` y `data/error.html`. `setHtmlPathPrefix()` permite ubicarlas en una subcarpeta.

La sección de recuperación ofrece dos operaciones diferentes. `scope=wifi` elimina únicamente `/wifi.json`. `scope=all` recorre LittleFS; respeta los archivos protegidos salvo que se envíe explícitamente `force=1`. El restablecimiento total del portal utiliza `force=1`, informa cuántos archivos encontró, eliminó o no pudo eliminar, y reinicia solamente si termina sin errores.

AWM cierra todos los handles que le pertenecen y reintenta cada eliminación tres veces. No puede cerrar de manera segura un archivo abierto por otra librería o componente del proyecto; en ese caso lo informa como fallido en lugar de declarar falsamente que el borrado fue completo.

## Políticas de fallback

- `NO_CREDENTIALS_ONLY`: valor predeterminado; abre el portal solo si no hay credenciales.
- `ON_FAIL`: lo abre cuando falla la conexión inicial.
- `SMART_RETRIES`: lo abre después de la cantidad configurada de fallos.
- `BUTTON_ONLY`: requiere una acción física o de la aplicación.
- `NEVER`: desactiva el fallback automático.

## Botón y LED

Los pines predeterminados son GPIO 0 para el botón activo en LOW y GPIO 2 para el LED.

- Mantener 2–5 segundos abre el portal.
- Mantener 5 segundos o más borra las credenciales y reinicia.
- Parpadeo lento: desconectado de Wi-Fi.
- Parpadeo rápido: conectando o escaneando.
- Encendido fijo: conectado a Wi-Fi.
- Doble parpadeo: conectado al Wi-Fi pero sin acceso a Internet verificado.
- Triple parpadeo: portal cautivo activo.

```cpp
AyresWiFiManager wifi(PIN_LED, PIN_BOTON);
```

## Credenciales y cifrado

Las credenciales se guardan en `/wifi.json`. El modo de almacenamiento se elige antes de `begin()` con un solo booleano; cuando el cifrado está activo, la clave debe tener exactamente 16 bytes:

```cpp
wifi.setCredentialEncryption(true, miClavePrivada);
wifi.begin();
```

Usá `false` en lugar de `true` para guardar en texto plano. Al cambiar el valor, un archivo existente se migra automáticamente en cualquier dirección siempre que se proporcione la misma clave. El método devuelve `false` y deja el cifrado desactivado si la clave no es válida.

No publiques una clave real en el repositorio. Con el cifrado desactivado, el archivo contiene solamente `{"ssid":"...","password":"..."}`. Con el cifrado activado, el registro completo se guarda como un único sobre autenticado AES-128-GCM representado por una cadena JSON opaca, por ejemplo `"QVdNA..."`; no expone nombres de campos ni metadatos del algoritmo. Los objetos cifrados anteriores y los registros CBC heredados se leen y migran automáticamente a este sobre. Esto no protege un dispositivo si se pueden extraer tanto el firmware como la clave; para amenazas mayores se necesita almacenamiento respaldado por hardware.

AWM deliberadamente no incorpora una clave AES. El sketch que usa la librería proporciona su propia clave de 16 bytes; cada proyecto debe decidir cómo provisionarla y protegerla. En repositorios públicos, mantené la clave real fuera del control de versiones (por ejemplo, en una configuración privada de compilación o mediante un proceso externo de provisionamiento).

### Reutilizar credenciales sin exponerlas

Si otro componente necesita comprobar la contraseÃ±a Wi-Fi para un acceso local, no hace falta obtenerla ni duplicarla:

```cpp
wifi.setAPCredentialsUsingStoredPassword("Control-Dispositivo");

if (wifi.verifyWiFiPassword(claveIngresada)) {
  // Acceso autorizado.
}
```

`setAPCredentialsUsingStoredPassword()` configura el SoftAP dentro de AWM y devuelve `false` si la clave guardada no es vÃ¡lida para WPA (entre 8 y 63 caracteres). `verifyWiFiPassword()` devuelve solamente un booleano y realiza una comparaciÃ³n cuyo trabajo no depende del primer carÃ¡cter diferente. `getWiFiPass()` se conserva por compatibilidad, pero las integraciones nuevas deberÃ­an preferir estos mÃ©todos para que AWM siga siendo el Ãºnico propietario del secreto.

## Logging

AWM utiliza `AyresLog.h` y conserva las macros `AWM_LOGE`, `AWM_LOGW`, `AWM_LOGI`, `AWM_LOGD` y `AWM_LOGV`.

```ini
build_flags =
  -D AWM_ENABLE_LOG=1
  -D AWM_LOG_LEVEL=3
  -D AWM_LOG_TAG=\"AWM\"
```

Con `AWM_ENABLE_LOG=0` el logging se elimina al compilar. Los niveles van de 1 (`ERROR`) a 5 (`VERBOSE`).

## Estructura del repositorio

```text
data/          Páginas personalizadas del portal
examples/      Ejemplos para Arduino y PlatformIO
src/           Headers públicos e implementación
.github/       Integración continua
```

El `src/main.cpp` local es un firmware de desarrollo ignorado por Git y no se distribuye con la librería.

## AyresNet GZIP Asset Compiler

El repositorio incluye `ayres_gzip.py`, una herramienta sin dependencias desarrollada por AyresNet. No tiene archivos de entrada ni carpetas de salida predefinidos. Al ejecutarla sin argumentos abre un asistente interactivo:

```bash
python ayres_gzip.py
```

El asistente permite agregar uno o varios archivos, carpetas o patrones y pregunta dónde crear el header `.h` o `.hpp`. Las mismas operaciones pueden automatizarse desde la terminal:

```bash
# Un archivo
python ayres_gzip.py web/index.html -o include/web_gz.h

# Varios archivos
python ayres_gzip.py web/index.html web/app.css web/app.js -o include/web_gz.h

# Una carpeta completa, incluyendo subcarpetas
python ayres_gzip.py web -r -o include/web_gz.h

# Solo algunos tipos y salida C++ portable
python ayres_gzip.py assets -r -I "*.html" -I "*.css" -o include/assets_gz.hpp -f cpp -p MI_APP
```

Para regenerar de forma explícita el portal embebido de esta librería:

```bash
python ayres_gzip.py data/index.html data/success.html data/error.html -o src/AWM_html_gz.h
```

La opción `--check`, usando los mismos argumentos, permite fallar en CI cuando el header está desactualizado. Los GZIP generados no incluyen fecha ni nombre del archivo de origen, por lo que producen diffs estables en Git. `python ayres_gzip.py -h` y `python ayres_gzip.py --help` muestran todas las opciones.

## Licencia

MIT © AyresNet. Consultá [LICENSE](LICENSE).
