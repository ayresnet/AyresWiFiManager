#ifndef AYRESSHELL_H
#define AYRESSHELL_H

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <WebServer.h> // Si lo estás usando en tu proyecto
#include <ArduinoJson.h> // ¡Necesario para manipular JSON!

// Directorio actual (si 'currentDir' se usa globalmente en otros archivos)
extern String currentDir;

// ==================== CLASE PRINCIPAL ====================
class AyresShell {
public:
  void begin();        // ← mensaje de bienvenida u otros inicializadores
  void handleInput();  // ← encapsula handleSerialCommands()
  void addCommand(const String& nombre, std::function<void(const String&)> callback);
};

// Comandos de shell existentes
void listDir(fs::FS &fs, const char * dirname);
void readFile(fs::FS &fs, const char * pathInput);
void deleteFile(fs::FS &fs, const char * pathInput);
void renameFile(fs::FS &fs, const char * oldNameInput, const char * newNameInput);
void moveFile(fs::FS &fs, const char * fromInput, const char * toInput);
void createDir(fs::FS &fs, const char * pathInput);
void removeDir(fs::FS &fs, const char * pathInput);
void clearScreen();
void help();
void handleSerialCommands();

// --- NUEVAS DECLARACIONES PARA JSON ---
// Funciones auxiliares para JSON
bool loadJsonFile(fs::FS &fs, const char *path, JsonDocument &doc);
bool saveJsonFile(fs::FS &fs, const char *path, const JsonDocument &doc);
// Función principal para actualizar un campo JSON por comando
bool updateJsonField(fs::FS &fs, const char *path, const char *key, const char *newValue);

#endif