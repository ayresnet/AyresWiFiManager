/*
 * AyresShell v1.0.0
 * --------------------------------------------------------
 * Consola serial interactiva para ESP32
 * Desarrollado por Daniel Cristian Salgado - AyresNet
 * https://github.com/ayresnet/AyresShell
 * --------------------------------------------------------
 * Esta consola permite ejecutar comandos personalizados,
 * interactuar con el sistema de archivos (LittleFS), y 
 * editar archivos JSON directamente desde el monitor serial.
 *
 * Compatible con Arduino IDE y PlatformIO.
 * Licencia: MIT
 */

#include "AyresShell.h"

// currentDir ya está declarado en tu .cpp, si es global y lo usas aquí, déjalo
String currentDir = "/";    // Directorio actual

// --- NUEVAS FUNCIONES PARA MANEJO DE JSON ---

// Tamaño del documento JSON. Ajusta si tus JSON son más grandes/complejos.
// Para wifi.json, 256 bytes es muy generoso.
const size_t JSON_DOC_SIZE = 256; 

// Función para leer y parsear un archivo JSON
bool loadJsonFile(fs::FS &fs, const char *path, JsonDocument &doc) {
  File file = fs.open(path, "r");
  if (!file) {
    Serial.println("❌ Error al abrir el archivo JSON para lectura.");
    return false;
  }

  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.print("❌ Error al parsear el JSON (posiblemente archivo corrupto o inválido): ");
    Serial.println(error.c_str());
    return false;
  }
  return true;
}

// Función para guardar un JsonDocument en un archivo
bool saveJsonFile(fs::FS &fs, const char *path, const JsonDocument &doc) {
  File file = fs.open(path, "w"); // Abrir en modo escritura para sobrescribir
  if (!file) {
    Serial.println("❌ Error al abrir el archivo JSON para escritura.");
    return false;
  }

  // serializeJsonPretty para una salida formateada, serializeJson para compacta
  if (serializeJsonPretty(doc, file) == 0) { 
    Serial.println("❌ Error al serializar el JSON.");
    file.close();
    return false;
  }

  file.close();
  // Serial.println("✅ Archivo JSON guardado exitosamente."); // Opcional, para debug
  return true;
}

// Función principal para actualizar un campo JSON
bool updateJsonField(fs::FS &fs, const char *pathInput, const char *key, const char *newValue) {
  String path = pathInput;
  path.trim();
  if (!path.startsWith("/")) path = currentDir + path; // Asegúrate de manejar rutas relativas

  Serial.print("Intentando actualizar campo '");
  Serial.print(key);
  Serial.print("' en archivo: [");
  Serial.print(path);
  Serial.println("]");

  StaticJsonDocument<JSON_DOC_SIZE> doc;

  // 1. Cargar y parsear el archivo JSON
  if (!loadJsonFile(fs, path.c_str(), doc)) {
    Serial.println("No se pudo cargar el archivo JSON para editar.");
    return false;
  }

  // 2. Modificar el campo específico
  // Si la clave no existe, ArduinoJson la creará automáticamente.
  doc[key] = newValue;
  Serial.print("Campo '");
  Serial.print(key);
  Serial.print("' actualizado a: '");
  Serial.print(newValue);
  Serial.println("'");

  // 3. Guardar el JsonDocument modificado
  if (saveJsonFile(fs, path.c_str(), doc)) {
    Serial.println("✅ Archivo JSON actualizado correctamente.");
    return true;
  } else {
    Serial.println("❌ Fallo al guardar el archivo JSON actualizado.");
    return false;
  }
}

// --- TUS FUNCIONES EXISTENTES (NO SE MODIFICAN) ---
void listDir(fs::FS &fs, const char * dirname) {
  File root = fs.open(dirname);
  if (!root || !root.isDirectory()) {
    Serial.println("No se pudo abrir el directorio.");
    return;
  }

  bool hasFiles = false;
  File file = root.openNextFile();
  while (file) {
    hasFiles = true;
    if (file.isDirectory()) {
      Serial.printf("     <dir>  %s\n", file.name());
    } else {
      Serial.printf("%10d  %s\n", file.size(), file.name());
    }
    file = root.openNextFile();
  }

  if (!hasFiles) {
    Serial.println("(No hay archivos en el sistema de archivos)");
  }

  Serial.printf("\nEspacio usado: %d bytes\n", LittleFS.usedBytes());
  Serial.printf("Espacio libre: %d bytes\n", LittleFS.totalBytes() - LittleFS.usedBytes());
  Serial.printf("Espacio total: %d bytes\n", LittleFS.totalBytes());
  Serial.println();
}

void readFile(fs::FS &fs, const char * pathInput) {
  String path = pathInput;
  path.trim();
  if (!path.startsWith("/")) path = currentDir + path;

  Serial.print("Intentando abrir: [");
  Serial.print(path);
  Serial.println("]");

  File file = fs.open(path, "r");
  if (!file) {
    Serial.println("Archivo no encontrado.");
    return;
  }

  while (file.available()) Serial.write(file.read());
  file.close();
  Serial.println(); // Salto de línea después de leer
}

void deleteFile(fs::FS &fs, const char * pathInput) {
  String path = pathInput;
  path.trim();
  if (!path.startsWith("/")) path = currentDir + path;
  if (fs.remove(path)) {
    Serial.println("Archivo eliminado.");
  } else {
    Serial.println("Error al eliminar.");
  }
}

void renameFile(fs::FS &fs, const char * oldNameInput, const char * newNameInput) {
  String oldName = oldNameInput;
  String newName = newNameInput;
  oldName.trim();
  newName.trim();
  if (!oldName.startsWith("/")) oldName = currentDir + oldName;
  if (!newName.startsWith("/")) newName = currentDir + newName;

  if (fs.rename(oldName.c_str(), newName.c_str())) {
    Serial.println("Archivo renombrado con éxito.");
  } else {
    Serial.println("Error al renombrar.");
  }
}

void moveFile(fs::FS &fs, const char * fromInput, const char * toInput) {
  String from = fromInput;
  String to = toInput;
  from.trim(); to.trim();

  if (!from.startsWith("/")) from = currentDir + from;
  if (!to.startsWith("/")) to = currentDir + to;

  File testDir = fs.open(to);
  if (to.endsWith("/") || (testDir && testDir.isDirectory())) {
    int slashIndex = from.lastIndexOf('/');
    String fileName = from.substring(slashIndex + 1);
    if (!to.endsWith("/")) to += "/";
    to += fileName;
  }

  if (fs.rename(from.c_str(), to.c_str())) {
    Serial.println("Archivo movido correctamente.");
  } else {
    Serial.println("Error al mover archivo.");
  }
}

void createDir(fs::FS &fs, const char * pathInput) {
  String path = pathInput;
  path.trim();
  if (!path.startsWith("/")) path = currentDir + path;
  if (fs.mkdir(path)) {
    Serial.println("Directorio creado correctamente.");
  } else {
    Serial.println("Error al crear directorio.");
  }
}

void removeDir(fs::FS &fs, const char * pathInput) {
  String path = pathInput;
  path.trim();
  if (!path.startsWith("/")) path = currentDir + path;
  if (fs.rmdir(path)) {
    Serial.println("Directorio eliminado.");
  } else {
    Serial.println("Error al eliminar el directorio (¿vacío?).");
  }
}

void clearScreen() {
  Serial.write(27);     // ESC
  Serial.print("[2J");  // Limpiar pantalla
  Serial.write(27);
  Serial.print("[H");   // Cursor al inicio

  for (int i = 0; i < 50; i++) Serial.println();
}

void help() {
  Serial.println("AyresNet Shell v1.0 - Comandos disponibles:");
  Serial.println("DIR                - Listar archivos + info");
  Serial.println("TYPE <archivo>     - Mostrar contenido de archivo");
  Serial.println("DEL <archivo>      - Eliminar archivo");
  Serial.println("REN <a> <b>        - Renombrar archivo");
  Serial.println("MV <a> <b>         - Mover archivo a otra carpeta");
  Serial.println("MKDIR <carpeta>    - Crear directorio");
  Serial.println("RMDIR <carpeta>    - Eliminar directorio vacío");
  Serial.println("CD <carpeta>       - Cambiar directorio (.. o / también)");
  Serial.println("JSONSET <ruta> <clave> \"<valor>\" - Editar campo en archivo JSON"); // ¡Nuevo comando!
  Serial.println("FORMAT             - Formatear LittleFS (¡BORRA TODO!)");
  Serial.println("CLS                - Limpiar pantalla");
  Serial.println("HELP               - Mostrar esta ayuda");
  Serial.println();
}

// --- TU handleSerialCommands() MODIFICADO ---
void handleSerialCommands() {
  static String input;
  static bool confirmFormat = false;

  if (Serial.available()) {
    char c = Serial.read();

    if (c == '\n') {
      input.trim();
      String upperInput = input;
      upperInput.toUpperCase();

      if (confirmFormat) {
        confirmFormat = false;
        if (upperInput == "S") {
          Serial.println("Formateando sistema de archivos...");
          if (LittleFS.format()) {
            Serial.println("Sistema de archivos formateado correctamente.");
          } else {
            Serial.println("Error al formatear el sistema de archivos.");
          }
        } else {
          Serial.println("Formato cancelado.");
        }
      }
      // --- AÑADIMOS LA LÓGICA PARA EL NUEVO COMANDO JSONSET ---
      else if (upperInput.startsWith("JSONSET ")) {
        // Formato esperado: jsonset <ruta_archivo> <clave> "<valor>"
        // Necesitamos parsear 3 argumentos.
        int firstSpace = input.indexOf(' '); // Después de "jsonset"
        int secondSpace = input.indexOf(' ', firstSpace + 1); // Después de <ruta_archivo>
        
        if (firstSpace != -1 && secondSpace != -1) {
          String path = input.substring(firstSpace + 1, secondSpace);
          String remaining = input.substring(secondSpace + 1);

          int keyEndIndex = remaining.indexOf(' ');
          if (keyEndIndex != -1) {
            String key = remaining.substring(0, keyEndIndex);
            String value = remaining.substring(keyEndIndex + 1);

            // Quitar comillas si el usuario las usó para el valor
            if (value.startsWith("\"") && value.endsWith("\"")) {
              value = value.substring(1, value.length() - 1);
            }
            
            updateJsonField(LittleFS, path.c_str(), key.c_str(), value.c_str());
          } else {
            Serial.println("Uso: JSONSET <ruta_archivo> <clave> \"<valor>\"");
            Serial.println("Ej: JSONSET /config.json ssid MiRed");
            Serial.println("Ej: JSONSET /creds.json password \"Mi Contraseña Secreta\"");
          }
        } else {
          Serial.println("Uso: JSONSET <ruta_archivo> <clave> \"<valor>\"");
          Serial.println("Ej: JSONSET /config.json ssid MiRed");
          Serial.println("Ej: JSONSET /creds.json password \"Mi Contraseña Secreta\"");
        }
      }
      // --- FIN DE LA LÓGICA JSONSET ---
      
      else if (upperInput == "DIR") {
        listDir(LittleFS, currentDir.c_str());
      }

      else if (upperInput.startsWith("TYPE ")) {
        String path = input.substring(5);
        readFile(LittleFS, path.c_str());
      }

      else if (upperInput.startsWith("DEL ")) {
        String path = input.substring(4);
        deleteFile(LittleFS, path.c_str());
      }

      else if (upperInput.startsWith("REN ")) {
        int sepIndex = input.indexOf(' ', 4);
        if (sepIndex > 0) {
          String oldName = input.substring(4, sepIndex);
          String newName = input.substring(sepIndex + 1);
          renameFile(LittleFS, oldName.c_str(), newName.c_str());
        } else {
          Serial.println("Uso: REN <viejo> <nuevo>");
        }
      }

      else if (upperInput.startsWith("MV ")) {
        int sepIndex = input.indexOf(' ', 3);
        if (sepIndex > 0) {
          String src = input.substring(3, sepIndex);
          String dst = input.substring(sepIndex + 1);
          moveFile(LittleFS, src.c_str(), dst.c_str());
        } else {
          Serial.println("Uso: MV <origen> <destino>");
        }
      }

      else if (upperInput.startsWith("MKDIR ")) {
        String path = input.substring(6);
        createDir(LittleFS, path.c_str());
      }

      else if (upperInput.startsWith("RMDIR ")) {
        String path = input.substring(6);
        removeDir(LittleFS, path.c_str());
      }

      else if (upperInput.startsWith("CD ")) {
        String path = input.substring(3);
        path.trim();

        if (path == "/") {
          currentDir = "/";
        } else if (path == "..") {
          if (currentDir != "/") {
            int lastSlash = currentDir.lastIndexOf('/', currentDir.length() - 2);
            currentDir = currentDir.substring(0, lastSlash + 1);
            if (currentDir.length() == 0) currentDir = "/";
          }
        } else {
          if (!path.startsWith("/")) path = currentDir + path;
          if (!path.endsWith("/")) path += "/";
          File dir = LittleFS.open(path);
          if (dir && dir.isDirectory()) {
            currentDir = path;
          } else {
            Serial.println("Directorio no válido o inexistente.");
          }
        }

        Serial.print("Directorio actual: ");
        Serial.println(currentDir);
      }

      else if (upperInput == "FORMAT") {
        Serial.println("¿Está seguro que desea formatear LittleFS? Esto BORRARÁ TODOS los archivos.");
        Serial.print("Escriba S para confirmar o N para cancelar: ");
        confirmFormat = true;
      }

      else if (upperInput == "CLS") {
        clearScreen();
      }

      else if (upperInput == "HELP") {
        help();
      }

      else if (input.length() > 0) {
        Serial.println("Comando no reconocido. Escriba 'HELP'.");
      }

      input = "";
    } else {
      input += c;
    }
  }
}

// ==================== IMPLEMENTACIÓN DE LA CLASE AYRESSHELL ====================

void AyresShell::begin() {
  Serial.println("🟢 AyresShell listo. Escribí HELP para ver los comandos.");
}

void AyresShell::handleInput() {
  handleSerialCommands();  // Llama a tu función global existente
}

void AyresShell::addCommand(const String& nombre, std::function<void(const String&)> callback) {
  // Funcionalidad futura. Por ahora no hacemos nada.
  // Podrías usar un std::map para registrar comandos personalizados.
}
