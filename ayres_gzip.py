#!/usr/bin/env python3
"""Compress one or more assets into a deterministic C++ header."""

from __future__ import annotations

import argparse
import fnmatch
import glob
import gzip
import io
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional, Sequence


APP_NAME = "AyresNet GZIP Asset Compiler"
APP_VERSION = "2.0.0"
AUTHOR = "AyresNet"
WEBSITE = "https://ayresnet.com"


@dataclass(frozen=True)
class Options:
    inputs: Sequence[str]
    output: Path
    format: str
    recursive: bool
    includes: Sequence[str]
    excludes: Sequence[str]
    prefix: str
    guard: Optional[str]
    level: int
    check: bool = False
    dry_run: bool = False


@dataclass(frozen=True)
class Asset:
    source: Path
    display_name: str
    symbol: str
    original: bytes
    compressed: bytes


def banner() -> str:
    width = 66
    return "\n".join(
        (
            "=" * width,
            f"  {APP_NAME} v{APP_VERSION}",
            f"  Developed by {AUTHOR} | {WEBSITE}",
            "  License: MIT",
            "=" * width,
        )
    )


def identifier(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").upper()
    if not cleaned:
        raise ValueError(f"No se puede crear un identificador desde {value!r}")
    if cleaned[0].isdigit():
        cleaned = "_" + cleaned
    return cleaned


def strip_quotes(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1]
    return value


def matches_any(path: Path, root: Path, patterns: Sequence[str]) -> bool:
    try:
        relative = path.relative_to(root).as_posix()
    except ValueError:
        relative = path.name
    return any(
        fnmatch.fnmatch(relative, pattern) or fnmatch.fnmatch(path.name, pattern)
        for pattern in patterns
    )


def files_from_directory(
    directory: Path,
    recursive: bool,
    includes: Sequence[str],
    excludes: Sequence[str],
) -> List[Path]:
    iterator: Iterable[Path] = directory.rglob("*") if recursive else directory.iterdir()
    files = []
    for path in iterator:
        if not path.is_file():
            continue
        if includes and not matches_any(path, directory, includes):
            continue
        if excludes and matches_any(path, directory, excludes):
            continue
        files.append(path)
    return sorted(files, key=lambda item: item.as_posix().lower())


def resolve_inputs(options: Options) -> List[Path]:
    output_path = options.output.resolve()
    resolved: List[Path] = []
    seen = set()

    for raw_spec in options.inputs:
        spec = strip_quotes(raw_spec)
        if not spec:
            continue

        if glob.has_magic(spec):
            matches = [Path(item) for item in glob.glob(spec, recursive=options.recursive)]
            candidates = []
            for match in matches:
                if match.is_dir():
                    candidates.extend(
                        files_from_directory(
                            match, options.recursive, options.includes, options.excludes
                        )
                    )
                elif match.is_file():
                    candidates.append(match)
        else:
            path = Path(spec).expanduser()
            if path.is_dir():
                candidates = files_from_directory(
                    path, options.recursive, options.includes, options.excludes
                )
            elif path.is_file():
                candidates = [path]
            else:
                raise FileNotFoundError(f"No existe: {spec}")

        if not candidates:
            raise FileNotFoundError(f"La entrada no contiene archivos: {spec}")

        for path in candidates:
            normalized = path.resolve()
            if normalized == output_path or normalized in seen:
                continue
            seen.add(normalized)
            resolved.append(normalized)

    if not resolved:
        raise ValueError("No se seleccionó ningún archivo para comprimir")
    return resolved


def common_root(paths: Sequence[Path]) -> Path:
    root = Path(os.path.commonpath([str(path) for path in paths]))
    return root.parent if root.is_file() else root


def display_name(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.name


def deterministic_gzip(content: bytes, level: int) -> bytes:
    output = io.BytesIO()
    with gzip.GzipFile(
        filename="", mode="wb", compresslevel=level, fileobj=output, mtime=0
    ) as stream:
        stream.write(content)
    return output.getvalue()


def load_assets(paths: Sequence[Path], prefix: str, level: int) -> List[Asset]:
    root = common_root(paths)
    symbol_prefix = identifier(prefix) + "_" if prefix else ""
    symbols = set()
    assets = []

    for path in paths:
        name = display_name(path, root)
        symbol = symbol_prefix + identifier(name) + "_GZ"
        if symbol in symbols:
            raise ValueError(
                f"Dos entradas generan el mismo símbolo {symbol}. Usá --prefix "
                "o rutas que conserven sus carpetas."
            )
        symbols.add(symbol)
        original = path.read_bytes()
        assets.append(
            Asset(path, name, symbol, original, deterministic_gzip(original, level))
        )
    return assets


def byte_rows(data: bytes, columns: int = 16) -> str:
    rows = []
    for offset in range(0, len(data), columns):
        chunk = data[offset : offset + columns]
        rows.append("  " + ", ".join(f"0x{byte:02X}" for byte in chunk) + ",")
    return "\n".join(rows)


def render_header(assets: Sequence[Asset], output: Path, options: Options) -> str:
    guard = identifier(options.guard) if options.guard else identifier(output.name)
    arduino = options.format == "arduino"
    storage = " PROGMEM" if arduino else ""
    uint32_type = "uint32_t" if arduino else "std::uint32_t"
    uint8_type = "uint8_t" if arduino else "std::uint8_t"
    include = (
        "#include <Arduino.h>"
        if arduino
        else "#include <cstddef>\n#include <cstdint>"
    )

    lines = [
        "/*",
        f" * Generated by {APP_NAME} v{APP_VERSION}",
        f" * Developed by {AUTHOR} | {WEBSITE}",
        " * License: MIT",
        " *",
        " * Generated file. Modify the source assets, not this header.",
        " */",
        "",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        include,
        "",
    ]

    for asset in assets:
        ratio = (len(asset.compressed) / len(asset.original) * 100) if asset.original else 0
        lines.extend(
            (
                f"// {asset.display_name}: {len(asset.original)} -> "
                f"{len(asset.compressed)} bytes ({ratio:.1f}%)",
                f"static const {uint32_type} {asset.symbol}_LEN = {len(asset.compressed)};",
                f"static const {uint8_type} {asset.symbol}[]{storage} = {{",
                byte_rows(asset.compressed),
                "};",
                "",
            )
        )

    lines.extend((f"#endif // {guard}", ""))
    return "\n".join(lines)


def atomic_write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_name = None
    try:
        with tempfile.NamedTemporaryFile(
            "w", encoding="utf-8", newline="\n", delete=False, dir=path.parent
        ) as temporary:
            temporary.write(content)
            temporary_name = temporary.name
        os.replace(temporary_name, path)
    finally:
        if temporary_name and os.path.exists(temporary_name):
            os.unlink(temporary_name)


def print_summary(assets: Sequence[Asset], output: Path) -> None:
    print(f"\nArchivos seleccionados: {len(assets)}")
    for asset in assets:
        ratio = (len(asset.compressed) / len(asset.original) * 100) if asset.original else 0
        print(
            f"  {asset.display_name:<32} {len(asset.original):>9} -> "
            f"{len(asset.compressed):>9} bytes ({ratio:>5.1f}%)"
        )
    total_in = sum(len(asset.original) for asset in assets)
    total_out = sum(len(asset.compressed) for asset in assets)
    ratio = (total_out / total_in * 100) if total_in else 0
    print(f"Total: {total_in} -> {total_out} bytes ({ratio:.1f}%)")
    print(f"Header de salida: {output}")


def ask(prompt: str, default: Optional[str] = None) -> str:
    suffix = f" [{default}]" if default is not None else ""
    value = input(f"{prompt}{suffix}: ").strip()
    return value if value else (default or "")


def ask_yes_no(prompt: str, default: bool = True) -> bool:
    marker = "S/n" if default else "s/N"
    while True:
        answer = input(f"{prompt} [{marker}]: ").strip().lower()
        if not answer:
            return default
        if answer in ("s", "si", "sí", "y", "yes"):
            return True
        if answer in ("n", "no"):
            return False
        print("Respondé S o N.")


def interactive_options() -> Options:
    print(banner())
    print(
        "\nIngresá archivos, carpetas o patrones, uno por línea.\n"
        "Ejemplos:\n"
        "  data/index.html\n"
        "  C:\\MiProyecto\\web\n"
        "  assets/**/*.css\n"
        "Presioná Enter en una línea vacía cuando termines.\n"
    )

    inputs: List[str] = []
    while True:
        value = strip_quotes(input(f"Entrada #{len(inputs) + 1}: "))
        if not value:
            break
        inputs.append(value)
    if not inputs:
        raise ValueError("No ingresaste ningún archivo o carpeta")

    recursive = ask_yes_no("¿Recorrer subcarpetas?", True)
    include_text = ask("Filtros a incluir, separados por coma (* = todos)", "*")
    exclude_text = ask("Filtros a excluir, separados por coma", "")
    output_text = ask("Ruta del header de salida (.h o .hpp)")
    if not output_text:
        raise ValueError("La ruta de salida es obligatoria")

    print("\nFormato de salida:")
    print("  1) Arduino / PlatformIO (PROGMEM)")
    print("  2) C++ portable")
    format_answer = ask("Elegí 1 o 2", "1")
    if format_answer not in ("1", "2"):
        raise ValueError("Formato inválido")

    prefix = ask("Prefijo opcional para los símbolos", "")
    level_text = ask("Nivel de compresión 0-9", "9")
    if not level_text.isdigit() or int(level_text) not in range(10):
        raise ValueError("El nivel debe estar entre 0 y 9")

    includes = [] if include_text.strip() == "*" else [
        item.strip() for item in include_text.split(",") if item.strip()
    ]
    excludes = [item.strip() for item in exclude_text.split(",") if item.strip()]
    return Options(
        inputs=inputs,
        output=Path(strip_quotes(output_text)).expanduser(),
        format="arduino" if format_answer == "1" else "cpp",
        recursive=recursive,
        includes=includes,
        excludes=excludes,
        prefix=prefix,
        guard=None,
        level=int(level_text),
    )


def create_parser() -> argparse.ArgumentParser:
    examples = f"""
Ejemplos:
  Modo interactivo:
    python ayres_gzip.py

  Un archivo:
    python ayres_gzip.py web/index.html -o include/web_gz.h

  Varios archivos:
    python ayres_gzip.py web/index.html web/app.css web/app.js -o include/web_gz.h

  Una carpeta completa, incluyendo subcarpetas:
    python ayres_gzip.py web -r -o include/web_gz.h

  Solo HTML, CSS y JS de una carpeta:
    python ayres_gzip.py web -r -I "*.html" -I "*.css" -I "*.js" -o include/web_gz.h

  Salida C++ portable con prefijo propio:
    python ayres_gzip.py assets -r -o include/assets_gz.hpp -f cpp -p MI_APP

  Verificar en CI sin modificar archivos:
    python ayres_gzip.py data/index.html data/success.html -o src/pages_gz.h --check

Developed by {AUTHOR} | {WEBSITE}
"""
    parser = argparse.ArgumentParser(
        prog="ayres_gzip.py",
        description=(
            "Comprime uno o varios archivos con GZIP y genera un header C++ "
            "determinista. Sin argumentos abre un asistente interactivo."
        ),
        epilog=examples,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "inputs", nargs="*", metavar="ENTRADA", help="Archivo, carpeta o patrón glob"
    )
    parser.add_argument(
        "-o", "--output", type=Path, help="Header de salida (.h o .hpp)"
    )
    parser.add_argument(
        "-r", "--recursive", action="store_true", help="Recorrer subcarpetas"
    )
    parser.add_argument(
        "-I", "--include", action="append", default=[], metavar="PATRÓN",
        help="Incluir patrón al expandir carpetas; se puede repetir",
    )
    parser.add_argument(
        "-X", "--exclude", action="append", default=[], metavar="PATRÓN",
        help="Excluir patrón al expandir carpetas; se puede repetir",
    )
    parser.add_argument("-p", "--prefix", default="", help="Prefijo de símbolos")
    parser.add_argument("--guard", help="Include guard personalizado")
    parser.add_argument(
        "-f", "--format", choices=("arduino", "cpp"), default="arduino",
        help="Formato Arduino PROGMEM o C++ portable (default: arduino)",
    )
    parser.add_argument(
        "-l", "--level", type=int, choices=range(10), default=9, metavar="0-9",
        help="Nivel de compresión (default: 9)",
    )
    parser.add_argument(
        "--check", action="store_true",
        help="Salir con código 1 si el header falta o está desactualizado",
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="Mostrar el resultado sin escribir"
    )
    parser.add_argument(
        "-v", "--version", action="version", version=f"{APP_NAME} {APP_VERSION}"
    )
    return parser


def options_from_cli(args: argparse.Namespace, parser: argparse.ArgumentParser) -> Options:
    if not args.inputs:
        parser.error("indicá al menos una ENTRADA o ejecutá sin opciones para el asistente")
    if args.output is None:
        parser.error("-o/--output es obligatorio en modo CLI")
    if args.output.suffix.lower() not in (".h", ".hpp"):
        parser.error("la salida debe terminar en .h o .hpp")
    return Options(
        inputs=args.inputs,
        output=args.output.expanduser(),
        format=args.format,
        recursive=args.recursive,
        includes=args.include,
        excludes=args.exclude,
        prefix=args.prefix,
        guard=args.guard,
        level=args.level,
        check=args.check,
        dry_run=args.dry_run,
    )


def execute(options: Options, confirm: bool = False) -> int:
    paths = resolve_inputs(options)
    assets = load_assets(paths, options.prefix, options.level)
    header = render_header(assets, options.output, options)
    encoded = header.encode("utf-8")
    current = options.output.read_bytes() if options.output.exists() else None

    print_summary(assets, options.output)
    if options.dry_run:
        print("\nSimulación terminada: no se escribió ningún archivo.")
        return 0

    if options.check:
        if current == encoded:
            print("\nOK: el header está actualizado.")
            return 0
        print("\nDESACTUALIZADO: ejecutá el comando sin --check.", file=sys.stderr)
        return 1

    if confirm and not ask_yes_no("¿Generar este header?", True):
        print("Operación cancelada.")
        return 0

    if current == encoded:
        print("\nSin cambios: el header ya está actualizado.")
    else:
        atomic_write(options.output, header)
        print(f"\nGenerado correctamente: {options.output}")
    return 0


def run(argv: Optional[Sequence[str]] = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    try:
        if not arguments:
            options = interactive_options()
            return execute(options, confirm=True)

        parser = create_parser()
        args = parser.parse_args(arguments)
        print(banner())
        return execute(options_from_cli(args, parser))
    except (EOFError, KeyboardInterrupt):
        print("\nOperación cancelada.", file=sys.stderr)
        return 130
    except (OSError, ValueError) as error:
        print(f"\nERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(run())
