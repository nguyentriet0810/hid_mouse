"""Run GCC's path analyzer on production units using the real IDF compile DB.

This is a general defect check, not a MISRA rule checker. Windows only.
Run after an ESP-IDF build/configure. Outputs stay in build/analysis/.
"""
from __future__ import annotations

import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import subprocess
import sys


def split_windows_command(command: str) -> list[str]:
    shell = ctypes.WinDLL("shell32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    shell.CommandLineToArgvW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype = ctypes.POINTER(wintypes.LPWSTR)
    kernel.LocalFree.argtypes = [wintypes.HLOCAL]
    kernel.LocalFree.restype = wintypes.HLOCAL
    count = ctypes.c_int()
    arguments = shell.CommandLineToArgvW(command, ctypes.byref(count))
    if not arguments:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        return [arguments[index] for index in range(count.value)]
    finally:
        kernel.LocalFree(ctypes.cast(arguments, wintypes.HLOCAL))


def main() -> int:
    project = Path(__file__).resolve().parent.parent
    database = project / "build" / "compile_commands.json"
    output = project / "build" / "analysis"
    output.mkdir(parents=True, exist_ok=True)
    entries = json.loads(database.read_text(encoding="utf-8"))
    failed = 0
    checked = 0
    for entry in entries:
        source = Path(entry["file"]).resolve()
        if not source.is_relative_to(project):
            continue
        relative = source.relative_to(project)
        if relative.parts[0] not in ("main", "components") or source.suffix != ".c":
            continue
        arguments = entry.get("arguments") or split_windows_command(entry["command"])
        output_index = arguments.index("-o") + 1
        arguments[output_index] = str(output / (source.stem + ".obj"))
        arguments.extend(["-fanalyzer", "-fdiagnostics-color=never"])
        result = subprocess.run(arguments, cwd=entry["directory"],
                                capture_output=True, text=True, errors="replace", check=False)
        diagnostic = result.stdout + result.stderr
        (output / (source.stem + ".log")).write_text(diagnostic, encoding="utf-8")
        checked += 1
        if result.returncode != 0:
            failed += 1
            print(f"FAIL: {relative}\n{diagnostic}")
        else:
            print(f"PASS: {relative}")
    print(f"GCC analyzer: {checked} production units, {failed} failed")
    expected = len(list((project / "main").glob("*.c"))) + len(list((project / "components").glob("*/*.c")))
    return 1 if failed or checked != expected else 0


if __name__ == "__main__":
    sys.exit(main())
