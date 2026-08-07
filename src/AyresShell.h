#pragma once
/*
 * AyresShell - DOS-style serial shell for ESP32/ESP8266
 * ======================================================
 * Lightweight interactive shell for LittleFS management.
 * Supports DIR, CD, DEL, DELTREE, ED, JEDIT, etc.
 *
 * (C) 2025 Daniel Cristian Salgado — AyresNet
 * MIT License — https://opensource.org/licenses/MIT
 *
 * GitHub: https://github.com/ayresnet/AyresShell
 *
 * ⚠️ Requirements:
 *   - ArduinoJson v6+
 *   - LittleFS enabled (board_build.filesystem = littlefs)
 *   - ESP8266: Arduino Core 3.0.0+ (LittleFS built-in)
 *   - ESP32: Arduino Core 2.0.0+
 *
 * 💡 Usage:
 *   #include "AyresShell.h"
 *   AyresShell shell;
 *   void setup() { Serial.begin(115200); shell.begin(); }
 *   void loop()  { shell.loop(); }
 *
 * --------------------------------------------------------
 * Version History
 *
 * v1.0.0 (2025-08)
 *   • Initial DOS-style shell for ESP32
 *   • Basic commands: DIR, CD, DEL, REN, MKDIR, RMDIR, TYPE
 *   • LittleFS support, FORMAT with confirmation
 *
 * v1.1.0 (2025-09)
 *   • Improved DIR with aligned columns and totals
 *   • Real DOS wildcards (*, ?, *.*), case-insensitive
 *   • DELTREE recursive delete + -f (force with remount)
 *   • JSONSET command (ArduinoJson integration)
 *   • CD.. without space support
 *   • Better path normalization (./, ../, absolute)
 *
 * v1.2.0 (2025-10)
 *   • ✅ Full ESP8266 compatibility
 *   • 🧼 Removed <vector> and STL from header
 *   • 📦 Lighter argument parsing (no std::vector)
 *   • 🔌 Portable FS abstraction (FS_IMPL = LittleFS)
 *   • ⚡ Reduced RAM usage and flash bloat
 *   • 🛠️ Safer file operations (yield(), shorter delays)
 *
 * v1.2.1 (2025-10)
 *   • 🔁 Consistent FS_IMPL usage en todo el código
 *   • 🧯 Cierre estricto de File antes de borrar/mover
 *   • 💾 Literales con F() para ahorrar RAM
 *   • 🧱 JSON buffer configurable (AYRESSHELL_JSON_BUF)
 *   • 🧭 splitArgs sin STL (max 4 args) y header liviano
 *   • 🛡️ DEL/DELTREE más robustos (reintentos + remount)
 *
 * v1.3.0 (2025-10)
 *   • ✍️ Nuevo ED: editor por líneas (edlin-like) para cualquier texto
 *   • 🌳 Nuevo JEDIT: editor de JSON en árbol (navegación, set/add/del/rename)
 *   • 🗑️ JSONSET eliminado (reemplazado por JEDIT)
 *   • 🔒 Guardado atómico (tmp + .bak + rename) en ED/JEDIT
 *   • ⛑️ Modo editor modal/bloqueante (shell vuelve al salir/guardar)
 * ======================================================
 */

#pragma once
/*
 * AyresShell - DOS-style serial shell for ESP32/ESP8266
 * ======================================================
 * Lightweight interactive shell for LittleFS management.
 * Supports DIR, CD, DEL, DELTREE, ED, JEDIT, etc.
 *
 * (C) 2025 Daniel Cristian Salgado — AyresNet
 * MIT License — https://opensource.org/licenses/MIT
 *
 * GitHub: https://github.com/ayresnet/AyresShell
 *
 * ⚠️ Requirements:
 *   - ArduinoJson v6+
 *   - LittleFS enabled (board_build.filesystem = littlefs)
 *   - ESP8266: Arduino Core 3.0.0+ (LittleFS built-in)
 *   - ESP32: Arduino Core 2.0.0+
 *
 * 💡 Usage:
 *   #include "AyresShell.h"
 *   AyresShell shell;
 *   void setup() { Serial.begin(115200); shell.begin(); }
 *   void loop()  { shell.loop(); }
 *
 * --------------------------------------------------------
 * Version History
 *
 * v1.0.0 (2025-08)
 *   • Initial DOS-style shell for ESP32
 *   • Basic commands: DIR, CD, DEL, REN, MKDIR, RMDIR, TYPE
 *   • LittleFS support, FORMAT with confirmation
 *
 * v1.1.0 (2025-09)
 *   • Improved DIR with aligned columns and totals
 *   • Real DOS wildcards (*, ?, *.*), case-insensitive
 *   • DELTREE recursive delete + -f (force with remount)
 *   • JSONSET command (ArduinoJson integration)
 *   • CD.. without space support
 *   • Better path normalization (./, ../, absolute)
 *
 * v1.2.0 (2025-10)
 *   • ✅ Full ESP8266 compatibility
 *   • 🧼 Removed <vector> and STL from header
 *   • 📦 Lighter argument parsing (no std::vector)
 *   • 🔌 Portable FS abstraction (FS_IMPL = LittleFS)
 *   • ⚡ Reduced RAM usage and flash bloat
 *   • 🛠️ Safer file operations (yield(), shorter delays)
 *
 * v1.2.1 (2025-10)
 *   • 🔁 Consistent FS_IMPL usage en todo el código
 *   • 🧯 Cierre estricto de File antes de borrar/mover
 *   • 💾 Literales con F() para ahorrar RAM
 *   • 🧱 JSON buffer configurable (AYRESSHELL_JSON_BUF)
 *   • 🧭 splitArgs sin STL (max 4 args) y header liviano
 *   • 🛡️ DEL/DELTREE más robustos (reintentos + remount)
 *
 * v1.3.0 (2025-10)
 *   • ✍️ Nuevo ED: editor por líneas (edlin-like) para cualquier texto
 *   • 🌳 Nuevo JEDIT: editor de JSON en árbol (navegación, set/add/del/rename)
 *   • 🗑️ JSONSET eliminado (reemplazado por JEDIT)
 *   • 🔒 Guardado atómico (tmp + .bak + rename) en ED/JEDIT
 *   • ⛑️ Modo editor modal/bloqueante (shell vuelve al salir/guardar)
 *
 * v1.3.1-hotfix1 (2025-11)
 *   • 🧹 DEL *.ext: evita abrir comodines y borra por patrón directamente
 *   • 📏 Forzado de rutas absolutas en remove/rename (fix logs “does not start
 * with /”) • 🔇 Menos ruido del VFS y recuento correcto tras borrar por comodín
 * ======================================================
 */

#include <Arduino.h>
#include <FS.h>

#if defined(ESP8266)
#include <LittleFS.h>
#define FS_IMPL LittleFS
#elif defined(ESP32)
#include <LittleFS.h>
#define FS_IMPL LittleFS
#else
#error "AyresShell only supports ESP8266 and ESP32."
#endif

#ifndef AYRESSHELL_VERSION
#define AYRESSHELL_VERSION "1.3.1-hotfix1"
#endif

#ifndef AYRESSHELL_JSON_BUF
#define AYRESSHELL_JSON_BUF 1024
#endif
#ifndef AYRESSHELL_ED_MAX_LINES
#define AYRESSHELL_ED_MAX_LINES 512
#endif
#ifndef AYRESSHELL_ED_MAX_LINE_LEN
#define AYRESSHELL_ED_MAX_LINE_LEN 256
#endif
#ifndef AYRESSHELL_JEDIT_MAX_DEPTH
#define AYRESSHELL_JEDIT_MAX_DEPTH 8
#endif

class AyresShell {
public:
  void begin(unsigned long baud = 115200); // Does NOT call Serial.begin()
  void loop();

  // ====== FreeRTOS (Zero Latency) ======
  void startTask(UBaseType_t priority = 1, int8_t core = 1);
  static void _taskLoop(void *param);

private:
  // ===== Estado general =====
  String _cwd = "/";
  String _inbuf;
  bool _confirmFormat = false;

  // Confirmaciones de borrado
  bool _confirmDel = false;
  String _confirmDelCmd;
  String _confirmDelArg;
  bool _confirmDelForce = false;

  // ===== Modos =====
  enum Mode : uint8_t {
    MODE_SHELL = 0,
    MODE_ED,
    MODE_ED_INSERT,
    MODE_ED_REPLACE,
    MODE_JEDIT,
    MODE_PROMPT_YN
  };
  Mode _mode = MODE_SHELL;

  // ===== ED =====
  String _ed_path;
  bool _ed_dirty = false;
  String *_ed_lines = nullptr;
  int _ed_count = 0;
  int _ed_cap = 0;
  int _ed_insert_at = -1;
  int _ed_replace_at = -1;
  String _ed_find;
  int _ed_find_pos = 0;
  bool _ed_quit_confirm = false;

  // ===== JEDIT =====
  String _j_path;
  bool _j_dirty = false;
  void *_j_doc = nullptr; // DynamicJsonDocument*
  String _j_keys[AYRESSHELL_JEDIT_MAX_DEPTH];
  bool _j_isIndex[AYRESSHELL_JEDIT_MAX_DEPTH];
  int _j_index[AYRESSHELL_JEDIT_MAX_DEPTH];
  int _j_depth = 0;
  bool _j_quit_confirm = false;

  // ===== Utils =====
  String normPath(const String &base, const String &inRaw);
  bool isDir(const String &path);
  bool existsPath(const String &path);
  String dosPath(const String &unixPath);
  void printPrompt();

  // Parser
  void splitArgs(const String &line, String args[], int &count,
                 int maxArgs = 4);
  void handleLine(String line);

  // ===== Comandos Shell =====
  void cmd_help();
  void cmd_cls();
  void cmd_dir(const String &arg);
  void cmd_cd(const String &arg);
  void cmd_mkdir(const String &arg);
  void cmd_rmdir(const String &arg);
  void cmd_type(const String &arg);
  void cmd_del(const String &arg);
  void cmd_ren(const String &a, const String &b);
  void cmd_mv(const String &a, const String &b);
  void cmd_format_query();
  void cmd_format_exec(bool yes);
  void cmd_deltree(const String &arg1, const String &arg2 = "");

  // Internos
  void do_del(const String &arg);
  void do_deltree(const String &arg, bool force);

  // Wildcards y helpers
  static bool dosMatch(const String &patternRaw, const String &nameRaw);
  static void splitDirAndPattern(const String &cwd, const String &arg,
                                 String &dirPath, String &pattern);
  static size_t delPattern(const String &dirPath, const String &pattern);
  static size_t deltreeImpl(const String &dirPath, size_t &dirs, size_t &files);

  // FS robusto
  static bool fsRemoveAggressive(const String &path, int tries = 3,
                                 int delayMs = 20);
  static bool fsRmdirRetry(const String &path, int tries = 3, int delayMs = 20);

  // ===== ED =====
  void cmd_ed(const String &path);
  void ed_enter(const String &path);
  void ed_leave();
  void ed_prompt();
  void ed_handle(const String &line);
  void ed_list(int fromLine = 1, int maxLines = 0);
  void ed_insert_begin(int atLine);
  void ed_insert_line(const String &text);
  void ed_insert_end();
  void ed_replace_begin(int line);
  void ed_replace_line(const String &text);
  bool ed_delete_range(int a, int b);
  bool ed_save();

  // ===== JEDIT =====
  void cmd_jedit(const String &path);
  void jedit_enter(const String &path);
  void jedit_leave();
  void jedit_prompt();
  void jedit_handle(const String &line);
  void jedit_show();
  bool jedit_openToken(const String &tok);
  bool jedit_up();
  bool jedit_set(const String &keyOrIndex, const String &valueText);
  bool jedit_add(const String &maybeKey, const String &valueText, bool hasKey);
  bool jedit_del(const String &keyOrIndex);
  bool jedit_rename(const String &oldKey, const String &newKey);
  bool jedit_save();

  // Helpers JEDIT
  void *jdoc();
  bool jcur_isObject();
  bool jcur_isArray();
  bool jresolve_index_key(int n, String &outKey);
  bool jparse_value(const String &txt, int &outI, double &outD, bool &outIsInt,
                    bool &outIsDouble, bool &outIsBool, bool &outIsNull,
                    String &outStr);
};
