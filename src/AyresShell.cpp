/*
 *  AyresShell — consola serial opcional para proyectos Arduino
 *  -------------------------------------------------------------------------
 *  Archivo   : AyresShell.cpp
 *  Versión   : 1.3.1
 *  Autor     : Daniel C. Salgado
 *  Empresa   : AyresNet IoT Systems
 *
 *  © 2025 AyresNet IoT Systems. Todos los derechos reservados.
 *
 *  Descripción
 *  -------------------------------------------------------------------------
 *  Intérprete de comandos (CLI) vía Puerto Serial.
 *  Funciones:
 *  - Gestión de archivos (LS, CAT, RM, MV) en LittleFS.
 *  - Editores de texto integrados (ED linea a linea, JEDIT para JSON).
 *  - Utilidades de sistema y depuración.
 */

#include "AyresShell.h"
#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// ===================== DIR Display Config =====================
static const int DIR_INDENT = 1;
static const int DIR_COL1_WIDTH = 12;
static const int DIR_GAP = 3;
static const int DIR_NAME_RULE = 54;

// ===================== Visual Helpers ======================
static inline void printSpaces(int n) {
  while (n-- > 0)
    Serial.write(' ');
}

static String humanSize(uint64_t n) {
  char buf[32];
  snprintf(buf, sizeof(buf), "(%u bytes)", (unsigned)n);
  return String(buf);
}

static void printDirHeaderAligned(const String &disp, const String &pattern) {
  if (pattern.length())
    Serial.printf(" Listando: %s (%s)\n\n", disp.c_str(), pattern.c_str());
  else
    Serial.printf(" Listando: %s\n\n", disp.c_str());

  printSpaces(DIR_INDENT);
  Serial.print(F("TIPO/TAM"));
  int hdrPad = DIR_COL1_WIDTH - (int)strlen("TIPO/TAM");
  if (hdrPad > 0)
    printSpaces(hdrPad);
  printSpaces(DIR_GAP);
  Serial.println(F("NOMBRE"));

  printSpaces(DIR_INDENT);
  for (int i = 0; i < DIR_COL1_WIDTH; i++)
    Serial.write('-');
  printSpaces(DIR_GAP);
  for (int i = 0; i < DIR_NAME_RULE; i++)
    Serial.write('-');
  Serial.println();
}

static void printDirRowAligned(const char *typeOrSize, const char *name) {
  printSpaces(DIR_INDENT);
  int len = (int)strlen(typeOrSize);
  int pad = DIR_COL1_WIDTH - len;
  if (pad < 0)
    pad = 0;
  printSpaces(pad);
  Serial.print(typeOrSize);
  printSpaces(DIR_GAP);
  Serial.println(name);
}

// ===== Ayudas descriptivas para ED y JEDIT =====
static void ed_printHelp() {
  Serial.println(F("[ED] Comandos:"));
  Serial.println(
      F("  L [n]           Listar desde la linea n (por defecto 1)"));
  Serial.println(F("  I n             Insertar antes de la linea n (modo "
                   "insercion; linea vacia=salir)"));
  Serial.println(
      F("  R n             Reemplazar la linea n (ingrese la nueva linea)"));
  Serial.println(F("  D n | D n-m     Borrar linea n o rango n..m"));
  Serial.println(
      F("  F texto         Buscar la proxima coincidencia de 'texto'"));
  Serial.println(
      F("  W               Guardar (escribe .tmp, renombra y deja .bak)"));
  Serial.println(
      F("  Q               Salir (si hay cambios sin guardar, pregunta)"));
  Serial.println(F("  HELP            Mostrar esta ayuda"));
}

static void jedit_printHelp() {
  Serial.println(F("[JEDIT] Comandos:"));
  Serial.println(F("  SHOW                     Mostrar el nodo actual"));
  Serial.println(F(
      "  OPEN <n|key>             Entrar a un hijo (indice 1-based o clave)"));
  Serial.println(F("  UP                       Subir un nivel"));
  Serial.println(F("  SET <idx|key> <valor>    Con comillas = string; sin "
                   "comillas = numero/bool/null/obj/array"));
  Serial.println(
      F("  ADD <key> <valor>        Agregar en objeto (clave + valor)"));
  Serial.println(
      F("  ADD <valor>              Agregar en arreglo (solo valor)"));
  Serial.println(
      F("  DEL <idx|key>            Eliminar un hijo por indice o clave"));
  Serial.println(
      F("  RENAME <old> <new>       Renombrar una clave (en objeto)"));
  Serial.println(F("  SAVE                     Guardar (escribe .tmp, renombra "
                   "y deja .bak)"));
  Serial.println(F("  QUIT                     Salir (si hay cambios sin "
                   "guardar, pregunta)"));
  Serial.println(F("  HELP                     Mostrar esta ayuda"));
}

// ===================== Path helper (HOTFIX) =====================
// Asegura rutas absolutas para LittleFS/VFS
static inline String ensureAbs(const String &p) {
  if (!p.length())
    return String("/");
  return (p[0] == '/') ? p : (String("/") + p);
}

// ===================== Robust FS Helpers (class methods)
// ======================
bool AyresShell::fsRmdirRetry(const String &p, int tries, int delayMs) {
  const String a = ensureAbs(p);
  for (int i = 0; i < tries; ++i) {
    if (FS_IMPL.rmdir(a))
      return true;
    delay(delayMs);
    yield();
  }
  FS_IMPL.end();
  delay(5);
  FS_IMPL.begin(true);
  return FS_IMPL.rmdir(a);
}

bool AyresShell::fsRemoveAggressive(const String &p, int tries, int delayMs) {
  const String a = ensureAbs(p);

  // Nombre temporal único sin consultar exists() para evitar logs del VFS
  String tmp = a + ".del." + String((uint32_t)millis(), HEX);

  // Dos pasadas con reintentos: intentar borrar directo primero
  for (int pass = 0; pass < 2; ++pass) {
    if (FS_IMPL.remove(a))
      return true;
    for (int i = 0; i < tries; ++i) {
      if (FS_IMPL.remove(a))
        return true;
      delay(delayMs);
      yield();
    }
    {
      File z = FS_IMPL.open(a, "r");
      if (z)
        z.close();
    }
  }

  // Plan B: rename -> remove(tmp)
  if (FS_IMPL.rename(a, tmp)) {
    for (int i = 0; i < tries; ++i) {
      if (FS_IMPL.remove(tmp))
        return true;
      delay(delayMs);
      yield();
    }
  }

  // Remount y último intento
  FS_IMPL.end();
  delay(5);
  FS_IMPL.begin(true);
  if (!FS_IMPL.exists(a))
    return true;
  if (FS_IMPL.remove(a))
    return true;
  if (FS_IMPL.exists(tmp))
    (void)FS_IMPL.remove(tmp);
  return false;
}

// ===================== Public API ======================
void AyresShell::begin(unsigned long /*baud*/) {
  if (!FS_IMPL.begin(true)) {
    Serial.println(F("❌ Falló montar LittleFS (se intentó formatear)."));
  }
  Serial.println();
  Serial.print(F("AYRESHELL Version "));
  Serial.println(F(AYRESSHELL_VERSION));
  Serial.println(F("(C) 2025 AyresNet IoT Systems — MIT License"));
  Serial.println();
  printPrompt();
}

// ====== FreeRTOS Task (Cero Latencia) ======
void AyresShell::startTask(UBaseType_t priority, int8_t core) {
  // Llamar begin() primero
  begin();

  // Crear tarea de FreeRTOS con prioridad y núcleo especificados
  xTaskCreatePinnedToCore(_taskLoop,    // Función de la tarea
                          "AyresShell", // Nombre
                          4096,         // Stack size
                          this,         // Parámetro (this)
                          priority,     // Prioridad
                          NULL,         // Handle (no lo necesitamos)
                          core // Core (-1 = cualquiera, 0 = core 0, 1 = core 1)
  );
}

void AyresShell::_taskLoop(void *param) {
  AyresShell *self = (AyresShell *)param;
  for (;;) {
    self->loop();
    vTaskDelay(1); // Yield mínimo para el scheduler
  }
}

// [OPTIMIZACIÓN] Procesar TODO el buffer serial de una sola vez
void AyresShell::loop() {
  // Mientras haya datos en el buffer, los leemos y procesamos
  // Esto evita el lag de "1 caracter por loop" si el loop principal es lento.
  while (Serial.available() > 0) {
    int c_int = Serial.read();
    if (c_int == -1)
      break; // Por seguridad
    char c = (char)c_int;

    if (c == '\r')
      continue;
    if (c == '\n') {
      Serial.println();
      String line = _inbuf;
      _inbuf = "";
      handleLine(line);
      if (_mode == MODE_SHELL && !_confirmFormat && !_confirmDel)
        printPrompt();
    } else if (c == 0x08 || c == 0x7F) {
      if (_inbuf.length()) {
        _inbuf.remove(_inbuf.length() - 1);
        Serial.print("\b \b");
        // Serial.write(0x1B); Serial.print("[D"); Serial.print(" ");
        // Serial.write(0x1B); Serial.print("[D");
      }
    } else if (isPrintable(c)) {
      _inbuf += c;
      Serial.print(c);
    }
    // Pequeño yield para evitar bloquear demasiado tiempo si llega una ráfaga
    // enorme (aunque en 115200 no debería pasar)
    if ((_inbuf.length() % 64) == 0)
      yield();
  }
}

// ===================== Utils ======================
String AyresShell::normPath(const String &base, const String &inRaw) {
  String in = inRaw;
  in.trim();
  String b = base;
  b.trim();

  String p;
  if (in.length() == 0) {
    p = b.length() ? b : "/";
  } else if (in.startsWith("/")) {
    p = in;
  } else {
    if (!b.length())
      b = "/";
    if (!b.startsWith("/"))
      b = "/" + b;
    if (!b.endsWith("/"))
      b += "/";
    p = b + in;
  }

  String out = "/";
  int start = (p[0] == '/') ? 1 : 0;
  for (int i = start; i <= (int)p.length(); ++i) {
    if (i == (int)p.length() || p[i] == '/') {
      if (i > start) {
        String seg = p.substring(start, i);
        if (seg == "..") {
          int last = out.lastIndexOf('/');
          if (last > 0)
            out = out.substring(0, last);
          else
            out = "/";
        } else if (seg != ".") {
          if (out != "/")
            out += "/";
          out += seg;
        }
      }
      start = i + 1;
    }
  }
  if (!out.length())
    out = "/";
  return out;
}

bool AyresShell::isDir(const String &path) {
  File f = FS_IMPL.open(path);
  bool result = (f && f.isDirectory());
  if (f)
    f.close();
  return result;
}

bool AyresShell::existsPath(const String &path) { return FS_IMPL.exists(path); }

String AyresShell::dosPath(const String &unixPath) {
  if (unixPath == "/")
    return "\\";
  String s = unixPath;
  if (s.endsWith("/"))
    s.remove(s.length() - 1);
  s.replace("/", "\\");
  return s;
}

// ===================== Wildcards DOS ======================
bool AyresShell::dosMatch(const String &patternRaw, const String &nameRaw) {
  String p = patternRaw;
  p.toUpperCase();
  String s = nameRaw;
  s.toUpperCase();
  if (p == "*.*")
    p = "*";

  size_t pi = 0, si = 0, star = (size_t)-1, match = 0;
  while (si < s.length()) {
    if (pi < p.length() && (p[pi] == '?' || p[pi] == s[si])) {
      pi++;
      si++;
    } else if (pi < p.length() && p[pi] == '*') {
      star = pi++;
      match = si;
    } else if (star != (size_t)-1) {
      pi = star + 1;
      si = ++match;
    } else
      return false;
  }
  while (pi < p.length() && p[pi] == '*')
    pi++;
  return pi == p.length();
}

void AyresShell::splitDirAndPattern(const String &cwd, const String &arg,
                                    String &dirPath, String &pattern) {
  if (!arg.length()) {
    dirPath = cwd;
    pattern = "";
    return;
  }

  if (arg.startsWith("/")) {
    int slash = arg.lastIndexOf('/');
    if (slash <= 0) {
      dirPath = "/";
      pattern = arg.substring(1);
    } else {
      dirPath = arg.substring(0, slash);
      pattern = arg.substring(slash + 1);
    }
    if (pattern == "")
      pattern = "*";
    return;
  }

  String full = cwd;
  if (!full.endsWith("/"))
    full += "/";
  full += arg;
  int slash = full.lastIndexOf('/');
  dirPath = (slash <= 0) ? "/" : full.substring(0, slash);
  pattern = full.substring(slash + 1);
  if (pattern == "")
    pattern = "*";
}

size_t AyresShell::delPattern(const String &dirPath, const String &pattern) {
  String dp = ensureAbs(dirPath); // HOTFIX: asegurar absoluto
  File dir = FS_IMPL.open(dp);
  if (!dir || !dir.isDirectory())
    return 0;

  size_t removed = 0;

  for (File e = dir.openNextFile(); e;) {
    String full = e.name(); // puede venir sin "/"
    bool isDir = e.isDirectory();
    String base = full;
    int s = base.lastIndexOf('/');
    if (s >= 0)
      base = base.substring(s + 1);
    e.close();

    full = ensureAbs(full); // HOTFIX: forzar absoluto

    if (!isDir && dosMatch(pattern, base)) {
      bool ok = fsRemoveAggressive(full);
      Serial.printf("[DEL] %s %s\n", ok ? "OK" : "Falló", full.c_str());
      if (ok)
        removed++;
    }
    e = dir.openNextFile();
  }
  dir.close();
  return removed;
}

// ---- DELTREE helpers ----
size_t AyresShell::deltreeImpl(const String &dirPath, size_t &dirs,
                               size_t &files) {
  String dp = ensureAbs(dirPath);
  // Ensure trailing slash for concatenation
  if (!dp.endsWith("/"))
    dp += "/";

  File dir = FS_IMPL.open(dp);
  if (!dir || !dir.isDirectory())
    return 0;

  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    String name = e.name();
    // Fix: If name is relative (no leading slash), prepend dirPath.
    // Even if it has a leading slash, check if it starts with the parent dir.
    // If e.name() is just "/file.txt" but we are in "/awm/", it's wrong if
    // standard LittleFS yields absolute. However, safest bet: if it doesn't
    // start with dp, prepend. NOTE: e.name() behavior varies by core.
    String p;
    if (name.startsWith("/")) {
      if (name.startsWith(dp)) {
        p = name; // Already full path
      } else {
        // Weird case: name starts with / but doesn't match parent.
        // Could be relative to root? Or just the name with a slash?
        // Let's assume it's just the name if it doesn't match parent path.
        // Strip leading slash to be safe
        p = dp + name.substring(1);
      }
    } else {
      p = dp + name;
    }

    if (e.isDirectory()) {
      e.close();
      deltreeImpl(p, dirs, files);
      if (fsRmdirRetry(p)) {
        dirs++;
      } else
        Serial.printf("[DELTREE] No se pudo borrar dir: %s\n",
                      ensureAbs(p).c_str());
    } else {
      e.close();
      if (fsRemoveAggressive(p)) {
        files++;
      } else
        Serial.printf("[DELTREE] No se pudo borrar archivo: %s\n",
                      ensureAbs(p).c_str());
    }
  }
  dir.close();
  return 1;
}

// ===================== Commands (Shell) ======================
void AyresShell::cmd_help() {
  Serial.println();
  Serial.print(F("AYRESHELL Version "));
  Serial.println(F(AYRESSHELL_VERSION));
  Serial.println(F("(C) 2025 AyresNet IoT Systems — MIT License"));
  Serial.println();

  Serial.println(F(" Comandos disponibles:\n"));
  Serial.println(
      F("  DIR [filtro|ruta]   Listar (DIR *.ext, DIR /ruta/*.json)"));
  Serial.println(
      F("  CD [ruta]           Cambiar/mostrar dir (CD.., ../, ./)"));
  Serial.println(F("  TYPE <file>         Ver archivo"));
  Serial.println(F("  DEL|ERASE|RM <pat>  Borrar archivo(s)"));
  Serial.println(F("  REN <a> <b>         Renombrar"));
  Serial.println(F("  MV <a> <b>          Mover/renombrar"));
  Serial.println(F("  MKDIR|MD <dir>      Crear directorio"));
  Serial.println(F("  RMDIR|RD <dir>      Borrar directorio (vacío)"));
  Serial.println(F("  DELTREE <dir>       Borrar directorio recursivo"));
  Serial.println(F("  DELTREE -f <dir>    Forzar (reintentos + remount)"));
  Serial.println(F("  EDIT/ED <file>      Editor por líneas"));
  Serial.println(F("  JEDIT <file.json>   Editor de JSON en árbol"));
  Serial.println(F("  FORMAT              Formatear LittleFS (confirmación)"));
  Serial.println(F("  CLS                 Limpiar pantalla"));
  Serial.println(F("  HELP                Ayuda"));
  Serial.println(F("  LS                  Alias de DIR"));
  Serial.println(F("  CAT <file>          Alias de TYPE"));
  Serial.println();
}

void AyresShell::cmd_cls() {
  // SOLUCIÓN FINAL: Solo imprimir saltos de línea.
  // La terminal del usuario NO soporta ANSI, así que borramos todo rastro de
  // 0x1B.
  for (int i = 0; i < 30; i++) {
    Serial.println();
  }
}

void AyresShell::cmd_dir(const String &arg) {
  if (arg.length()) {
    String pAbs =
        ensureAbs(normPath(_cwd, arg)); // HOTFIX: forzar absoluto para file
    File ftest = FS_IMPL.open(pAbs);
    if (ftest && !ftest.isDirectory()) {
      String base = pAbs;
      int s = base.lastIndexOf('/');
      if (s >= 0)
        base = base.substring(s + 1);
      String disp = "C:" + dosPath(_cwd);
      if (!disp.endsWith("\\"))
        disp += "\\";
      printDirHeaderAligned(disp, "");
      String sz = humanSize(ftest.size());
      printDirRowAligned(sz.c_str(), base.c_str());
      Serial.println();
      Serial.printf("  1 archivo(s), 0 directorio(s)\n");
      Serial.printf("  Usado: %u  Libre: %u  Total: %u\n",
                    (unsigned)FS_IMPL.usedBytes(),
                    (unsigned)(FS_IMPL.totalBytes() - FS_IMPL.usedBytes()),
                    (unsigned)FS_IMPL.totalBytes());
      ftest.close();
      return;
    }
    if (ftest)
      ftest.close();
  }

  String dirPath, pattern;
  if (!arg.length()) {
    dirPath = _cwd;
    pattern = "";
  } else {
    String dAbs = ensureAbs(normPath(_cwd, arg));
    File dchk = FS_IMPL.open(dAbs);
    if (dchk && dchk.isDirectory()) {
      dirPath = dAbs;
      pattern = "";
      dchk.close();
    } else {
      if (dchk)
        dchk.close();
      splitDirAndPattern(_cwd, arg, dirPath, pattern);
    }
  }

  File dir = FS_IMPL.open(ensureAbs(dirPath));
  if (!dir || !dir.isDirectory()) {
    Serial.println(F("No se pudo abrir el directorio."));
    return;
  }

  String disp = "C:" + dosPath(dirPath);
  if (!disp.endsWith("\\"))
    disp += "\\";
  printDirHeaderAligned(disp, pattern);

  size_t files = 0, dirs = 0;

  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    String full = e.name();
    String base = full;
    int s = base.lastIndexOf('/');
    if (s >= 0)
      base = base.substring(s + 1);
    if (pattern.length() && !dosMatch(pattern, base)) {
      e.close();
      continue;
    }

    if (e.isDirectory()) {
      printDirRowAligned("<DIR>", base.c_str());
      dirs++;
    } else {
      String sz = humanSize(e.size());
      printDirRowAligned(sz.c_str(), base.c_str());
      files++;
    }
    e.close();
  }
  dir.close();

  Serial.println();
  Serial.printf("  %u archivo(s), %u directorio(s)\n", (unsigned)files,
                (unsigned)dirs);
  Serial.printf("  Usado: %u  Libre: %u  Total: %u\n",
                (unsigned)FS_IMPL.usedBytes(),
                (unsigned)(FS_IMPL.totalBytes() - FS_IMPL.usedBytes()),
                (unsigned)FS_IMPL.totalBytes());
}

void AyresShell::cmd_cd(const String &arg) {
  if (!arg.length()) {
    String disp = "C:" + dosPath(_cwd);
    if (!disp.endsWith("\\"))
      disp += "\\";
    Serial.println(disp);
    return;
  }
  String target = ensureAbs(normPath(_cwd, arg));
  File d = FS_IMPL.open(target);
  if (d && d.isDirectory()) {
    _cwd = target;
    if (!_cwd.endsWith("/"))
      _cwd += "/";
    String disp = "C:" + dosPath(_cwd);
    if (!disp.endsWith("\\"))
      disp += "\\";
    Serial.print(F("Directorio actual: "));
    Serial.println(disp);
  } else {
    Serial.println(F("Directorio no válido o inexistente."));
  }
  if (d)
    d.close();
}

void AyresShell::cmd_mkdir(const String &arg) {
  if (!arg.length()) {
    Serial.println(F("Uso: MKDIR <dir>"));
    return;
  }
  String p = ensureAbs(normPath(_cwd, arg));
  if (existsPath(p)) {
    Serial.println(F("Ya existe."));
    return;
  }
  Serial.println(FS_IMPL.mkdir(p) ? F("OK") : F("Error creando directorio."));
}

void AyresShell::cmd_rmdir(const String &arg) {
  if (!arg.length()) {
    Serial.println(F("Uso: RMDIR <dir>"));
    return;
  }
  String p = ensureAbs(normPath(_cwd, arg));
  File d = FS_IMPL.open(p);
  if (!d || !d.isDirectory()) {
    Serial.println(F("No es un directorio."));
    if (d)
      d.close();
    return;
  }
  bool isEmpty = true;
  File e = d.openNextFile();
  if (e) {
    isEmpty = false;
    e.close();
  }
  d.close();
  if (!isEmpty) {
    Serial.println(F("Directorio no vacío."));
    return;
  }
  Serial.println(FS_IMPL.rmdir(p) ? F("OK") : F("Error borrando directorio."));
}

void AyresShell::cmd_type(const String &arg) {
  if (!arg.length()) {
    Serial.println(F("Uso: TYPE <archivo>"));
    return;
  }
  String p = ensureAbs(normPath(_cwd, arg)); // HOTFIX: absoluto
  File f = FS_IMPL.open(p, "r");
  if (!f || f.isDirectory()) {
    Serial.println(F("No se pudo abrir."));
    if (f)
      f.close();
    return;
  }
  while (f.available())
    Serial.write(f.read());
  f.close();
  Serial.println();
}

// ---------- DEL ----------
void AyresShell::cmd_del(const String &arg) {
  if (!arg.length()) {
    Serial.println(F("Uso: DEL <archivo|*.ext|*.*|/ruta/*.ext>"));
    return;
  }
  _confirmDel = true;
  _confirmDelCmd = "DEL";
  _confirmDelArg = arg;
  _confirmDelForce = false;
  Serial.printf("¿Seguro que desea eliminar \"%s\"? [S/N]: ", arg.c_str());
}

void AyresShell::do_del(const String &arg) {
  // HOTFIX: Si hay comodines, no intentes abrir como archivo
  if (arg.indexOf('*') >= 0 || arg.indexOf('?') >= 0) {
    String dirPath, pattern;
    splitDirAndPattern(_cwd, arg, dirPath, pattern);
    if (!pattern.length()) {
      Serial.println(F("Uso: DEL <archivo|patrón>"));
      return;
    }
    size_t n = delPattern(dirPath, pattern);
    Serial.printf("Archivos eliminados: %u\n", (unsigned)n);
    return;
  }

  // Caso "archivo exacto"
  String p = ensureAbs(normPath(_cwd, arg));
  File t = FS_IMPL.open(p);
  if (t && !t.isDirectory()) {
    t.close();
    bool ok = fsRemoveAggressive(p);
    Serial.println(ok ? F("OK") : F("Error borrando (archivo en uso)."));
    return;
  }
  if (t)
    t.close();

  // Si no era archivo, intentar como patrón igualmente
  String dirPath, pattern;
  splitDirAndPattern(_cwd, arg, dirPath, pattern);
  if (!pattern.length()) {
    Serial.println(F("Uso: DEL <archivo|patrón>"));
    return;
  }
  size_t n = delPattern(dirPath, pattern);
  Serial.printf("Archivos eliminados: %u\n", (unsigned)n);
}

void AyresShell::cmd_ren(const String &a, const String &b) {
  if (!a.length() || !b.length()) {
    Serial.println(F("Uso: REN <viejo> <nuevo>"));
    return;
  }

  String src = ensureAbs(normPath(_cwd, a));
  String dst = ensureAbs(normPath(_cwd, b));

  if (!existsPath(src)) {
    Serial.println(F("Origen no existe."));
    return;
  }

  if (FS_IMPL.rename(src, dst)) {
    Serial.println(F("OK"));
    return;
  }

  if (existsPath(dst)) {
    Serial.println(F("Destino ya existe."));
    return;
  }
  Serial.println(F("Error renombrando."));
}

void AyresShell::cmd_mv(const String &a, const String &b) {
  if (!a.length() || !b.length()) {
    Serial.println(F("Uso: MV <origen> <destino>"));
    return;
  }
  String src = ensureAbs(normPath(_cwd, a));
  String dst = ensureAbs(normPath(_cwd, b));
  File d = FS_IMPL.open(dst);
  if (d && d.isDirectory()) {
    int slash = src.lastIndexOf('/');
    String fname = (slash >= 0) ? src.substring(slash + 1) : src;
    if (!dst.endsWith("/"))
      dst += "/";
    dst += fname;
    d.close();
  } else if (d) {
    d.close();
  }
  Serial.println(FS_IMPL.rename(src, dst) ? F("OK")
                                          : F("Error moviendo/renombrando."));
}

void AyresShell::cmd_format_query() {
  Serial.println(F("¿Seguro que desea FORMATEAR LittleFS? BORRA TODO."));
  Serial.print(F("Escriba S para confirmar o N para cancelar: "));
  _confirmFormat = true;
}

void AyresShell::cmd_format_exec(bool yes) {
  _confirmFormat = false;
  if (!yes) {
    Serial.println(F("Formato cancelado."));
    return;
  }
  Serial.println(F("Formateando LittleFS..."));
  Serial.println(FS_IMPL.format() ? F("OK") : F("Fallo al formatear."));
}

// ===================== Parser helpers ======================
// Parser general (sin comillas) — ya existente
void AyresShell::splitArgs(const String &line, String args[], int &count,
                           int maxArgs) {
  count = 0;
  String tok;
  bool inQuote = false;

  for (unsigned i = 0; i < line.length() && count < maxArgs; ++i) {
    char c = line[i];
    if (c == '"') {
      inQuote = !inQuote;
      continue;
    }
    if (!inQuote && isspace((unsigned char)c)) {
      if (tok.length()) {
        args[count++] = tok;
        tok = "";
      }
    } else {
      tok += c;
    }
  }
  if (tok.length() && count < maxArgs)
    args[count++] = tok;
}

// Nuevo: parser que **preserva** comillas (para JEDIT)
static void splitArgsKeepQuotes(const String &line, String args[], int &count,
                                int maxArgs = 4) {
  count = 0;
  String tok;
  bool inQuote = false;

  for (unsigned i = 0; i < line.length() && count < maxArgs; ++i) {
    char c = line[i];
    if (c == '"') {
      inQuote = !inQuote;
      tok += c;
      continue;
    }
    if (!inQuote && isspace((unsigned char)c)) {
      if (tok.length()) {
        args[count++] = tok;
        tok = "";
      }
    } else {
      tok += c;
    }
  }
  if (tok.length() && count < maxArgs)
    args[count++] = tok;
}

void AyresShell::handleLine(String line) {
  line.trim();

  if (_mode == MODE_SHELL && _confirmDel) {
    char c = line.length() ? toupper(line[0]) : 'N';
    bool force = _confirmDelForce;
    String cmd = _confirmDelCmd;
    String arg = _confirmDelArg;
    _confirmDel = false;
    _confirmDelForce = false;
    _confirmDelCmd = "";
    _confirmDelArg = "";
    Serial.println();
    if (c == 'S' || c == 'Y') {
      if (cmd == "DEL")
        do_del(arg);
      else if (cmd == "DELTREE")
        do_deltree(arg, force);
    } else {
      Serial.println(F("Eliminación cancelada."));
    }
    return;
  }

  if (_mode == MODE_SHELL && line.length() == 0) {
    if (_confirmFormat) {
      Serial.println();
      _confirmFormat = false;
    }
    return;
  }

  // Redirección a ED (incluye submodo PROMPT_YN del ED)
  if (_mode == MODE_ED || _mode == MODE_ED_INSERT || _mode == MODE_ED_REPLACE ||
      (_mode == MODE_PROMPT_YN && _ed_quit_confirm)) {
    ed_handle(line);
    return;
  }
  // Redirección a JEDIT
  if (_mode == MODE_JEDIT) {
    jedit_handle(line);
    return;
  }

  if (_confirmFormat) {
    char c = toupper(line[0]);
    cmd_format_exec(c == 'S');
    return;
  }

  // Soporte "CD.."
  if (_mode == MODE_SHELL && line.length() >= 4 &&
      line.substring(0, 4).equalsIgnoreCase("CD..")) {
    line = "CD .." + line.substring(4);
  }

  String args[4];
  int argc = 0;
  splitArgs(line, args, argc, 4);
  if (argc == 0)
    return;

  String CMD = args[0];
  CMD.toUpperCase();
  auto A = [&](int i) -> String { return (i < argc) ? args[i] : String(); };

  if (CMD == "HELP" || CMD == "?")
    cmd_help();
  else if (CMD == "CLS")
    cmd_cls();
  else if (CMD == "DIR" || CMD == "LS")
    cmd_dir(A(1));
  else if (CMD == "CD")
    cmd_cd(A(1));
  else if (CMD == "MKDIR" || CMD == "MD")
    cmd_mkdir(A(1));
  else if (CMD == "RMDIR" || CMD == "RD")
    cmd_rmdir(A(1));
  else if (CMD == "TYPE" || CMD == "CAT")
    cmd_type(A(1));
  else if (CMD == "DEL" || CMD == "ERASE" || CMD == "RM")
    cmd_del(A(1));
  else if (CMD == "REN" || CMD == "RENAME")
    cmd_ren(A(1), A(2));
  else if (CMD == "MV")
    cmd_mv(A(1), A(2));
  else if (CMD == "DELTREE")
    cmd_deltree(A(1), A(2));
  else if (CMD == "FORMAT")
    cmd_format_query();
  else if (CMD == "EDIT" || CMD == "ED")
    cmd_ed(A(1));
  else if (CMD == "JEDIT")
    cmd_jedit(A(1));
  else
    Serial.println(F("Comando no reconocido. Escribí HELP."));
}

void AyresShell::printPrompt() {
  Serial.print("C:");
  Serial.print(dosPath(_cwd));
  if (!_cwd.endsWith("/"))
    Serial.print("\\");
  Serial.print("> ");
}

void AyresShell::cmd_deltree(const String &arg1, const String &arg2) {
  String pathArg;
  bool force = false;

  if (arg1 == "-f") {
    force = true;
    pathArg = arg2;
  } else {
    // Caso normal: solo directorio en arg1 (o arg1="-fdir" si no hubieran
    // espacios, pero splitArgs separa) OJO: Si el usuario escribe por error
    // "DELTREE -fdir" (pegado), arg1="-fdir". Mantenemos logica de backward
    // compatibility por si acaso.
    if (arg1.startsWith("-f") && arg1.length() > 2) {
      force = true;
      pathArg = arg1.substring(2);
    } else {
      pathArg = arg1;
    }
  }

  pathArg.trim();
  if (!pathArg.length()) {
    Serial.println(F("Uso: DELTREE <directorio>  |  DELTREE -f <directorio>"));
    return;
  }

  if (!pathArg.length()) {
    Serial.println(F("Uso: DELTREE <directorio>  |  DELTREE -f <directorio>"));
    return;
  }
  _confirmDel = true;
  _confirmDelCmd = "DELTREE";
  _confirmDelArg = pathArg;
  _confirmDelForce = force;
  Serial.printf("¿Seguro que desea eliminar recursivamente \"%s\"%s? [S/N]: ",
                pathArg.c_str(), force ? " (FORZADO)" : "");
}

void AyresShell::do_deltree(const String &arg, bool force) {
  String pathArg = arg;
  pathArg.trim();
  if (!pathArg.length()) {
    Serial.println(F("Uso: DELTREE <directorio>  |  DELTREE -f <directorio>"));
    return;
  }

  String p = ensureAbs(normPath(_cwd, pathArg));
  if (p == "/") {
    Serial.println(F("❌ No se permite DELTREE sobre la raíz /."));
    return;
  }

  File d = FS_IMPL.open(p);
  if (!d || !d.isDirectory()) {
    Serial.println(F("No es un directorio."));
    if (d)
      d.close();
    return;
  }
  d.close();

  if (force) {
    FS_IMPL.end();
    delay(5);
    FS_IMPL.begin(true);
  }

  size_t dirs = 0, files = 0;
  deltreeImpl(p, dirs, files);
  bool ok = fsRmdirRetry(p);
  if (ok)
    dirs++;

  Serial.printf("DELTREE %s: %u archivo(s), %u directorio(s) eliminados\n",
                ok ? "OK" : "PARCIAL", (unsigned)files, (unsigned)dirs);
}

// ===================== ED (Editor por líneas) ======================
void AyresShell::cmd_ed(const String &path) {
  if (!path.length()) {
    Serial.println(F("Uso: ED <archivo>"));
    return;
  }
  ed_enter(ensureAbs(normPath(_cwd, path)));
}

void AyresShell::ed_enter(const String &path) {
  _ed_path = path;
  _ed_dirty = false;
  _ed_insert_at = -1;
  _ed_replace_at = -1;
  _ed_find = "";
  _ed_find_pos = 0;

  if (_ed_lines) {
    delete[] _ed_lines;
    _ed_lines = nullptr;
  }
  _ed_cap = AYRESSHELL_ED_MAX_LINES;
  _ed_lines = new String[_ed_cap];
  _ed_count = 0;

  File f = FS_IMPL.open(_ed_path, "r");
  if (f) {
    while (f.available() && _ed_count < _ed_cap) {
      String line = f.readStringUntil('\n');
      if (line.endsWith("\r"))
        line.remove(line.length() - 1);
      if (line.length() > AYRESSHELL_ED_MAX_LINE_LEN)
        line = line.substring(0, AYRESSHELL_ED_MAX_LINE_LEN);
      _ed_lines[_ed_count++] = line;
      yield();
    }
    f.close();
  }

  _mode = MODE_ED;
  Serial.printf("[ED] Editando: %s  (líneas: %d)\n", _ed_path.c_str(),
                _ed_count);
  ed_printHelp();
  ed_prompt();
}

void AyresShell::ed_leave() {
  if (_ed_lines) {
    delete[] _ed_lines;
    _ed_lines = nullptr;
  }
  _ed_count = 0;
  _ed_cap = 0;
  _mode = MODE_SHELL;
  printPrompt();
}

void AyresShell::ed_prompt() { Serial.print("ED> "); }

void AyresShell::ed_list(int fromLine, int maxLines) {
  if (fromLine < 1)
    fromLine = 1;
  if (fromLine > _ed_count) {
    Serial.println(F("(fin)"));
    return;
  }
  int to = (_ed_count);
  if (maxLines > 0 && fromLine + maxLines - 1 < to)
    to = fromLine + maxLines - 1;
  for (int i = fromLine; i <= to; ++i) {
    Serial.printf("%4d: ", i);
    Serial.println(_ed_lines[i - 1]);
    yield();
  }
}

void AyresShell::ed_insert_begin(int atLine) {
  if (atLine < 1)
    atLine = 1;
  if (atLine > _ed_count + 1)
    atLine = _ed_count + 1;
  _ed_insert_at = atLine - 1;
  _mode = MODE_ED_INSERT;
  Serial.printf("Insertando antes de la línea %d. Línea vacía para terminar.\n",
                atLine);
  Serial.print("I> ");
}

void AyresShell::ed_insert_line(const String &text) {
  if (text.length() == 0) {
    ed_insert_end();
    return;
  }
  if (_ed_count >= _ed_cap) {
    Serial.println(F("Capacidad máxima alcanzada."));
    Serial.print("I> ");
    return;
  }

  for (int i = _ed_count; i > _ed_insert_at; --i)
    _ed_lines[i] = _ed_lines[i - 1];
  _ed_lines[_ed_insert_at] = (text.length() > AYRESSHELL_ED_MAX_LINE_LEN)
                                 ? text.substring(0, AYRESSHELL_ED_MAX_LINE_LEN)
                                 : text;
  _ed_count++;
  _ed_insert_at++;
  _ed_dirty = true;
  Serial.print("I> ");
}

void AyresShell::ed_insert_end() {
  _mode = MODE_ED;
  _ed_insert_at = -1;
  Serial.println(F("(fin inserción)"));
  ed_prompt();
}

void AyresShell::ed_replace_begin(int line) {
  if (line < 1 || line > _ed_count) {
    Serial.println(F("Línea fuera de rango."));
    ed_prompt();
    return;
  }
  _ed_replace_at = line - 1;
  _mode = MODE_ED_REPLACE;
  Serial.printf("Reemplazar línea %d. Escriba la nueva línea:\n", line);
  Serial.print("R> ");
}

void AyresShell::ed_replace_line(const String &text) {
  _ed_lines[_ed_replace_at] =
      (text.length() > AYRESSHELL_ED_MAX_LINE_LEN)
          ? text.substring(0, AYRESSHELL_ED_MAX_LINE_LEN)
          : text;
  _ed_dirty = true;
  _mode = MODE_ED;
  _ed_replace_at = -1;
  Serial.println(F("(reemplazada)"));
  ed_prompt();
}

bool AyresShell::ed_delete_range(int a, int b) {
  if (a < 1)
    a = 1;
  if (b > _ed_count)
    b = _ed_count;
  if (a > b)
    return false;
  int n = b - a + 1;
  for (int i = a - 1; i + n < _ed_count; ++i)
    _ed_lines[i] = _ed_lines[i + n];
  _ed_count -= n;
  if (_ed_count < 0)
    _ed_count = 0;
  _ed_dirty = true;
  return true;
}

bool AyresShell::ed_save() {
  String tmp = _ed_path + ".tmp";
  String bak = _ed_path + ".bak";

  File f = FS_IMPL.open(tmp, "w");
  if (!f) {
    Serial.println(F("❌ No se pudo abrir tmp para escritura."));
    return false;
  }
  for (int i = 0; i < _ed_count; ++i) {
    f.print(_ed_lines[i]);
    f.write('\n');
    if ((i & 15) == 0)
      yield();
  }
  f.close();

  if (FS_IMPL.exists(bak))
    FS_IMPL.remove(bak);
  if (FS_IMPL.exists(_ed_path) && !FS_IMPL.rename(_ed_path, bak)) {
    Serial.println(F("⚠️ No se pudo crear .bak (continuando)."));
  }
  if (!FS_IMPL.rename(tmp, _ed_path)) {
    Serial.println(F("❌ Falló el rename de tmp → archivo."));
    return false;
  }

  _ed_dirty = false;
  Serial.println(F("✅ Guardado."));
  return true;
}

void AyresShell::ed_handle(const String &lineIn) {
  // Confirmación de salida (submodo PROMPT_YN)
  if (_mode == MODE_PROMPT_YN && _ed_quit_confirm) {
    char c = lineIn.length() ? toupper(lineIn[0]) : 'N';
    _ed_quit_confirm = false;
    _mode = MODE_ED;
    if (c == 'S' || c == 'Y') {
      Serial.println(F("(saliendo de ED)"));
      ed_leave();
    } else {
      ed_prompt();
    }
    return;
  }

  if (_mode == MODE_ED_INSERT) {
    ed_insert_line(lineIn);
    return;
  }
  if (_mode == MODE_ED_REPLACE) {
    ed_replace_line(lineIn);
    return;
  }

  String line = lineIn;
  if (!line.length()) {
    ed_prompt();
    return;
  }

  String a[4];
  int n = 0;
  splitArgs(line, a, n, 4);
  String CMD = a[0];
  CMD.toUpperCase();

  if (CMD == "HELP" || CMD == "H" || CMD == "?") {
    ed_printHelp();
    ed_prompt();
  } else if (CMD == "L") {
    int from = (n >= 2) ? a[1].toInt() : 1;
    ed_list(from, 0);
    ed_prompt();
  } else if (CMD == "I") {
    int at = (n >= 2) ? a[1].toInt() : (_ed_count + 1);
    ed_insert_begin(at);
  } else if (CMD == "R") {
    if (n < 2) {
      Serial.println(F("Uso: R <n>"));
      ed_prompt();
      return;
    }
    ed_replace_begin(a[1].toInt());
  } else if (CMD == "D") {
    if (n < 2) {
      Serial.println(F("Uso: D <n>  |  D n-m"));
      ed_prompt();
      return;
    }
    int dash = a[1].indexOf('-');
    bool ok = false;
    if (dash < 0)
      ok = ed_delete_range(a[1].toInt(), a[1].toInt());
    else {
      int A = a[1].substring(0, dash).toInt();
      int B = a[1].substring(dash + 1).toInt();
      ok = ed_delete_range(A, B);
    }
    Serial.println(ok ? F("OK") : F("Rango inválido."));
    ed_prompt();
  } else if (CMD == "F") {
    if (n < 2) {
      Serial.println(F("Uso: F <texto>"));
      ed_prompt();
      return;
    }
    String needle = a[1];
    if (_ed_find != needle) {
      _ed_find = needle;
      _ed_find_pos = 0;
    }
    bool found = false;
    for (int i = _ed_find_pos; i < _ed_count; ++i) {
      if (_ed_lines[i].indexOf(_ed_find) >= 0) {
        Serial.printf("Encontrado en línea %d: %s\n", i + 1,
                      _ed_lines[i].c_str());
        _ed_find_pos = i + 1;
        found = true;
        break;
      }
      yield();
    }
    if (!found) {
      Serial.println(F("(no hay más coincidencias)"));
      _ed_find_pos = 0;
    }
    ed_prompt();
  } else if (CMD == "W") {
    ed_save();
    ed_prompt();
  } else if (CMD == "Q") {
    if (_ed_dirty) {
      Serial.print(F("Cambios sin guardar. ¿Salir? [S/N]: "));
      _ed_quit_confirm = true;
      _mode = MODE_PROMPT_YN;
      return;
    }
    Serial.println(F("(saliendo de ED)"));
    ed_leave();
  } else {
    ed_printHelp();
    ed_prompt();
  }
}

// ===================== JEDIT (Editor JSON) ======================
void AyresShell::cmd_jedit(const String &path) {
  if (!path.length()) {
    Serial.println(F("Uso: JEDIT <archivo.json>"));
    return;
  }
  jedit_enter(ensureAbs(normPath(_cwd, path)));
}

void *AyresShell::jdoc() { return _j_doc; }

void AyresShell::jedit_enter(const String &path) {
  _j_path = path;
  _j_dirty = false;
  _j_depth = 0;
  _j_quit_confirm = false;

  if (_j_doc) {
    delete (DynamicJsonDocument *)_j_doc;
    _j_doc = nullptr;
  }
  _j_doc = (void *)new DynamicJsonDocument(AYRESSHELL_JSON_BUF);

  File f = FS_IMPL.open(_j_path, "r");
  DeserializationError err;
  if (f) {
    err = deserializeJson(*(DynamicJsonDocument *)_j_doc, f);
    f.close();
  } else {
    ((DynamicJsonDocument *)_j_doc)->to<JsonObject>();
  }

  if (err) {
    Serial.print(F("⚠️ JSON inválido o vacío: "));
    Serial.println(err.c_str());
    ((DynamicJsonDocument *)_j_doc)->to<JsonObject>();
  }

  _mode = MODE_JEDIT;
  Serial.printf("[JEDIT] Editando: %s\n", _j_path.c_str());
  jedit_printHelp();
  jedit_show();
  jedit_prompt();
}

void AyresShell::jedit_leave() {
  if (_j_doc) {
    delete (DynamicJsonDocument *)_j_doc;
    _j_doc = nullptr;
  }
  _j_depth = 0;
  _mode = MODE_SHELL;
  printPrompt();
}

void AyresShell::jedit_prompt() { Serial.print("JEDIT> "); }

bool AyresShell::jcur_isObject() {
  JsonVariant cur = ((DynamicJsonDocument *)_j_doc)->as<JsonVariant>();
  for (int d = 0; d < _j_depth; ++d) {
    if (_j_isIndex[d])
      cur = cur[_j_index[d]];
    else
      cur = cur[_j_keys[d]];
  }
  return cur.is<JsonObject>();
}

bool AyresShell::jcur_isArray() {
  JsonVariant cur = ((DynamicJsonDocument *)_j_doc)->as<JsonVariant>();
  for (int d = 0; d < _j_depth; ++d) {
    if (_j_isIndex[d])
      cur = cur[_j_index[d]];
    else
      cur = cur[_j_keys[d]];
  }
  return cur.is<JsonArray>();
}

bool AyresShell::jresolve_index_key(int n, String &outKey) {
  JsonVariant cur = ((DynamicJsonDocument *)_j_doc)->as<JsonVariant>();
  for (int d = 0; d < _j_depth; ++d) {
    if (_j_isIndex[d])
      cur = cur[_j_index[d]];
    else
      cur = cur[_j_keys[d]];
  }
  if (!cur.is<JsonObject>())
    return false;
  int i = 1;
  for (JsonPair kv : cur.as<JsonObject>()) {
    if (i == n) {
      outKey = kv.key().c_str();
      return true;
    }
    i++;
  }
  return false;
}

void AyresShell::jedit_show() {
  JsonVariant cur = ((DynamicJsonDocument *)_j_doc)->as<JsonVariant>();
  for (int d = 0; d < _j_depth; ++d) {
    if (_j_isIndex[d])
      cur = cur[_j_index[d]];
    else
      cur = cur[_j_keys[d]];
  }

  if (cur.is<JsonObject>()) {
    int i = 1;
    Serial.println("{");
    for (JsonPair kv : cur.as<JsonObject>()) {
      JsonVariant v = kv.value();
      Serial.printf(" [%d] %-12s : ", i, kv.key().c_str());
      if (v.is<const char *>())
        Serial.printf("\"%s\"\n", v.as<const char *>());
      else if (v.is<bool>())
        Serial.println(v.as<bool>() ? "true" : "false");
      else if (v.is<long>() || v.is<int>() || v.is<unsigned long>())
        Serial.println((long)v.as<long>());
      else if (v.is<double>() || v.is<float>())
        Serial.println(v.as<double>(), 6);
      else if (v.is<JsonObject>())
        Serial.println("{ ... }");
      else if (v.is<JsonArray>())
        Serial.println("[ ... ]");
      else
        Serial.println("null");
      i++;
      yield();
    }
    Serial.println("}");
  } else if (cur.is<JsonArray>()) {
    JsonArray arr = cur.as<JsonArray>();
    for (size_t i = 0; i < arr.size(); ++i) {
      JsonVariant v = arr[i];
      Serial.printf(" [%d] ", (int)i + 1);
      if (v.is<const char *>())
        Serial.printf("\"%s\"\n", v.as<const char *>());
      else if (v.is<bool>())
        Serial.println(v.as<bool>() ? "true" : "false");
      else if (v.is<long>() || v.is<int>() || v.is<unsigned long>())
        Serial.println((long)v.as<long>());
      else if (v.is<double>() || v.is<float>())
        Serial.println(v.as<double>(), 6);
      else if (v.is<JsonObject>())
        Serial.println("{ ... }");
      else if (v.is<JsonArray>())
        Serial.println("[ ... ]");
      else
        Serial.println("null");
      yield();
    }
  } else {
    if (cur.is<const char *>())
      Serial.printf("\"%s\"\n", cur.as<const char *>());
    else if (cur.is<bool>())
      Serial.println(cur.as<bool>() ? "true" : "false");
    else if (cur.is<long>() || cur.is<int>() || cur.is<unsigned long>())
      Serial.println((long)cur.as<long>());
    else if (cur.is<double>() || cur.is<float>())
      Serial.println(cur.as<double>(), 6);
    else
      Serial.println("null");
  }
}

// ===== Helpers para JEDIT: comillas y asignación de valor =====
static inline bool _isQuoted(const String &s) {
  return s.length() >= 2 && s[0] == '"' && s[s.length() - 1] == '"';
}
static inline String _unquote(const String &s) {
  return _isQuoted(s) ? s.substring(1, s.length() - 1) : s;
}
static bool _looksInt(const String &s) {
  if (!s.length())
    return false;
  int i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
  if (i >= (int)s.length())
    return false;
  for (; i < (int)s.length(); ++i)
    if (!isDigit((unsigned char)s[i]))
      return false;
  return true;
}
static bool _looksFloat(const String &s) {
  if (!s.length())
    return false;
  bool dot = false, exp = false;
  int i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
  for (; i < (int)s.length(); ++i) {
    char c = s[i];
    if (isDigit((unsigned char)c))
      continue;
    if (c == '.' && !dot && !exp) {
      dot = true;
      continue;
    }
    if ((c == 'e' || c == 'E') && !exp) {
      exp = true;
      if (i + 1 < (int)s.length() && (s[i + 1] == '+' || s[i + 1] == '-'))
        ++i;
      continue;
    }
    return false;
  }
  return dot || exp;
}
static void _jsonAssignRespectingQuotes(JsonVariant target, const String &raw) {
  if (_isQuoted(raw)) {
    target.set(_unquote(raw));
    return;
  }
  String low = raw;
  low.trim();
  low.toLowerCase();
  if (low == "true") {
    target.set(true);
    return;
  }
  if (low == "false") {
    target.set(false);
    return;
  }
  if (low == "null") {
    target.clear();
    return;
  }
  if ((low.startsWith("{") && low.endsWith("}")) ||
      (low.startsWith("[") && low.endsWith("]"))) {
    StaticJsonDocument<768> tmp;
    DeserializationError err = deserializeJson(tmp, raw);
    if (!err) {
      target.set(tmp.as<JsonVariant>());
      return;
    }
  }
  if (_looksInt(raw)) {
    target.set((long long)raw.toInt());
    return;
  }
  if (_looksFloat(raw)) {
    target.set(raw.toFloat());
    return;
  }
  target.set(raw);
}

bool AyresShell::jedit_openToken(const String &tok) {
  JsonVariant cur = ((DynamicJsonDocument *)_j_doc)->as<JsonVariant>();
  for (int d = 0; d < _j_depth; ++d) {
    if (_j_isIndex[d])
      cur = cur[_j_index[d]];
    else
      cur = cur[_j_keys[d]];
  }

  if (tok.length() == 0)
    return false;

  bool isNum = true;
  for (unsigned i = 0; i < tok.length(); ++i)
    if (!isdigit((unsigned char)tok[i])) {
      isNum = false;
      break;
    }
  if (isNum) {
    int n = tok.toInt();
    if (cur.is<JsonArray>()) {
      JsonArray arr = cur.as<JsonArray>();
      if (n < 1 || n > (int)arr.size())
        return false;
      _j_isIndex[_j_depth] = true;
      _j_index[_j_depth] = n - 1;
      _j_keys[_j_depth] = "";
      _j_depth++;
      return true;
    } else if (cur.is<JsonObject>()) {
      String key;
      if (!jresolve_index_key(n, key))
        return false;
      _j_isIndex[_j_depth] = false;
      _j_index[_j_depth] = -1;
      _j_keys[_j_depth] = key;
      _j_depth++;
      return true;
    }
    return false;
  }

  if (!cur.is<JsonObject>())
    return false;
  JsonObject obj = cur.as<JsonObject>();
  if (!obj.containsKey(tok))
    return false;
  _j_isIndex[_j_depth] = false;
  _j_index[_j_depth] = -1;
  _j_keys[_j_depth] = tok;
  _j_depth++;
  return true;
}

bool AyresShell::jedit_up() {
  if (_j_depth <= 0)
    return false;
  _j_depth--;
  return true;
}

bool AyresShell::jedit_set(const String &keyOrIndex, const String &valueText) {
  JsonVariant cur = ((DynamicJsonDocument *)_j_doc)->as<JsonVariant>();
  for (int d = 0; d < _j_depth; ++d) {
    if (_j_isIndex[d])
      cur = cur[_j_index[d]];
    else
      cur = cur[_j_keys[d]];
  }

  if (cur.is<JsonObject>()) {
    String key = keyOrIndex;
    bool num = true;
    for (unsigned i = 0; i < key.length(); ++i)
      if (!isdigit((unsigned char)key[i])) {
        num = false;
        break;
      }
    if (num) {
      String mapped;
      if (jresolve_index_key(key.toInt(), mapped))
        key = mapped;
    }
    JsonVariant slot = cur.as<JsonObject>()[key];
    _jsonAssignRespectingQuotes(slot, valueText);
    _j_dirty = true;
    return true;
  } else if (cur.is<JsonArray>()) {
    bool num = true;
    for (unsigned i = 0; i < keyOrIndex.length(); ++i)
      if (!isdigit((unsigned char)keyOrIndex[i])) {
        num = false;
        break;
      }
    if (!num)
      return false;
    int idx = keyOrIndex.toInt() - 1;
    JsonArray arr = cur.as<JsonArray>();
    if (idx < 0 || idx >= (int)arr.size())
      return false;
    JsonVariant slot = arr[idx];
    _jsonAssignRespectingQuotes(slot, valueText);
    _j_dirty = true;
    return true;
  }
  return false;
}

bool AyresShell::jedit_add(const String &maybeKey, const String &valueText,
                           bool hasKey) {
  JsonVariant cur = ((DynamicJsonDocument *)_j_doc)->as<JsonVariant>();
  for (int d = 0; d < _j_depth; ++d) {
    if (_j_isIndex[d])
      cur = cur[_j_index[d]];
    else
      cur = cur[_j_keys[d]];
  }

  if (cur.is<JsonObject>() && hasKey) {
    JsonVariant slot = cur.as<JsonObject>()[maybeKey];
    _jsonAssignRespectingQuotes(slot, valueText);
    _j_dirty = true;
    return true;
  } else if (cur.is<JsonArray>() && !hasKey) {
    String raw = valueText;
    String low = raw;
    low.trim();
    low.toLowerCase();
    JsonArray arr = cur.as<JsonArray>();
    if (_isQuoted(raw)) {
      arr.add(_unquote(raw));
      _j_dirty = true;
      return true;
    }
    if (low == "true") {
      arr.add(true);
      _j_dirty = true;
      return true;
    }
    if (low == "false") {
      arr.add(false);
      _j_dirty = true;
      return true;
    }
    if (low == "null") {
      arr.add(nullptr);
      _j_dirty = true;
      return true;
    }
    if (_looksInt(raw)) {
      arr.add((long long)raw.toInt());
      _j_dirty = true;
      return true;
    }
    if (_looksFloat(raw)) {
      arr.add(raw.toFloat());
      _j_dirty = true;
      return true;
    }
    if ((low.startsWith("{") && low.endsWith("}")) ||
        (low.startsWith("[") && low.endsWith("]"))) {
      StaticJsonDocument<768> tmp;
      if (!deserializeJson(tmp, raw)) {
        arr.add(tmp.as<JsonVariant>());
        _j_dirty = true;
        return true;
      }
    }
    arr.add(raw);
    _j_dirty = true;
    return true;
  }
  return false;
}

bool AyresShell::jedit_del(const String &keyOrIndex) {
  JsonVariant cur = ((DynamicJsonDocument *)_j_doc)->as<JsonVariant>();
  for (int d = 0; d < _j_depth; ++d) {
    if (_j_isIndex[d])
      cur = cur[_j_index[d]];
    else
      cur = cur[_j_keys[d]];
  }

  if (cur.is<JsonObject>()) {
    String key = keyOrIndex;
    bool num = true;
    for (unsigned i = 0; i < key.length(); ++i)
      if (!isdigit((unsigned char)key[i])) {
        num = false;
        break;
      }
    if (num) {
      String mapped;
      if (jresolve_index_key(key.toInt(), mapped))
        key = mapped;
    }
    cur.as<JsonObject>().remove(key);
    _j_dirty = true;
    return true;
  } else if (cur.is<JsonArray>()) {
    bool num = true;
    for (unsigned i = 0; i < keyOrIndex.length(); ++i)
      if (!isdigit((unsigned char)keyOrIndex[i])) {
        num = false;
        break;
      }
    if (!num)
      return false;
    int idx = keyOrIndex.toInt() - 1;
    JsonArray arr = cur.as<JsonArray>();
    if (idx < 0 || idx >= (int)arr.size())
      return false;
    arr.remove(idx);
    _j_dirty = true;
    return true;
  }
  return false;
}

bool AyresShell::jedit_rename(const String &oldKey, const String &newKey) {
  JsonVariant cur = ((DynamicJsonDocument *)_j_doc)->as<JsonVariant>();
  for (int d = 0; d < _j_depth; ++d) {
    if (_j_isIndex[d])
      cur = cur[_j_index[d]];
    else
      cur = cur[_j_keys[d]];
  }
  if (!cur.is<JsonObject>())
    return false;

  JsonObject obj = cur.as<JsonObject>();
  if (!obj.containsKey(oldKey)) {
    String mapped;
    if (!jresolve_index_key(oldKey.toInt(), mapped))
      return false;
    return jedit_rename(mapped, newKey);
  }
  JsonVariant v = obj[oldKey];
  obj[newKey] = v;
  obj.remove(oldKey);
  _j_dirty = true;
  return true;
}

bool AyresShell::jedit_save() {
  String tmp = _j_path + ".tmp";
  String bak = _j_path + ".bak";

  File f = FS_IMPL.open(tmp, "w");
  if (!f) {
    Serial.println(F("❌ No se pudo abrir tmp para escritura."));
    return false;
  }
  if (serializeJsonPretty(*(DynamicJsonDocument *)_j_doc, f) == 0) {
    Serial.println(F("❌ Error serializando JSON."));
    f.close();
    return false;
  }
  f.close();

  if (FS_IMPL.exists(bak))
    FS_IMPL.remove(bak);
  if (FS_IMPL.exists(_j_path) && !FS_IMPL.rename(_j_path, bak)) {
    Serial.println(F("⚠️ No se pudo crear .bak (continuando)."));
  }
  if (!FS_IMPL.rename(tmp, _j_path)) {
    Serial.println(F("❌ Falló el rename de tmp → archivo."));
    return false;
  }

  _j_dirty = false;
  Serial.println(F("✅ JSON guardado."));
  return true;
}

void AyresShell::jedit_handle(const String &lineIn) {
  if (!lineIn.length()) {
    jedit_prompt();
    return;
  }

  if (_j_quit_confirm && _mode == MODE_JEDIT) {
    char c = toupper(lineIn[0]);
    _j_quit_confirm = false;
    if (c == 'S' || c == 'Y') {
      Serial.println(F("(saliendo de JEDIT)"));
      jedit_leave();
      return;
    }
    jedit_prompt();
    return;
  }

  String a[4];
  int n = 0;
  splitArgsKeepQuotes(lineIn, a, n, 4); // preserva comillas
  String CMD = a[0];
  CMD.toUpperCase();

  if (CMD == "HELP" || CMD == "H" || CMD == "?") {
    jedit_printHelp();
    jedit_prompt();
  } else if (CMD == "SHOW") {
    jedit_show();
    jedit_prompt();
  } else if (CMD == "OPEN") {
    if (n < 2) {
      Serial.println(F("Uso: OPEN <n|key>"));
      jedit_prompt();
      return;
    }
    if (jedit_openToken(a[1])) {
      jedit_show();
    } else
      Serial.println(F("No se pudo abrir (índice/clave inválida)"));
    jedit_prompt();
  } else if (CMD == "UP") {
    if (!jedit_up())
      Serial.println(F("(ya en raíz)"));
    jedit_show();
    jedit_prompt();
  } else if (CMD == "SET") {
    if (n < 3) {
      Serial.println(
          F("Uso: SET <idx|key> <valor> (use comillas para strings)"));
      jedit_prompt();
      return;
    }
    if (jedit_set(a[1], a[2]))
      Serial.println(F("OK"));
    else
      Serial.println(F("Error en SET"));
    jedit_prompt();
  } else if (CMD == "ADD") {
    if (n < 2) {
      Serial.println(F("Uso: ADD <key> <valor>  |  ADD <valor> (array)"));
      jedit_prompt();
      return;
    }
    bool hasKey = (n >= 3);
    if (hasKey ? jedit_add(a[1], a[2], true) : jedit_add("", a[1], false))
      Serial.println(F("OK"));
    else
      Serial.println(F("Error en ADD"));
    jedit_prompt();
  } else if (CMD == "DEL") {
    if (n < 2) {
      Serial.println(F("Uso: DEL <idx|key>"));
      jedit_prompt();
      return;
    }
    if (jedit_del(a[1]))
      Serial.println(F("OK"));
    else
      Serial.println(F("Error en DEL"));
    jedit_prompt();
  } else if (CMD == "RENAME") {
    if (n < 3) {
      Serial.println(F("Uso: RENAME <oldKey> <newKey>"));
      jedit_prompt();
      return;
    }
    if (jedit_rename(a[1], a[2]))
      Serial.println(F("OK"));
    else
      Serial.println(F("Error en RENAME"));
    jedit_prompt();
  } else if (CMD == "SAVE") {
    jedit_save();
    jedit_prompt();
  } else if (CMD == "QUIT") {
    if (_j_dirty) {
      Serial.print(F("Cambios sin guardar. ¿Salir? [S/N]: "));
      _j_quit_confirm = true;
      return;
    }
    Serial.println(F("(saliendo de JEDIT)"));
    jedit_leave();
  } else {
    jedit_printHelp();
    jedit_prompt();
  }
}
