"""Compile the real private CCC persistence adapter against fault-injected NVS."""
from pathlib import Path
import argparse
import os
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--compiler", default="gcc")
    arguments = parser.parse_args()
    project = arguments.project.resolve()
    tests = Path(__file__).resolve().parent
    build = tests / "build"
    build.mkdir(exist_ok=True)
    executable = build / ("test_ble_ccc.exe" if os.name == "nt" else "test_ble_ccc")
    component = project / "components" / "ble_mouse"
    flags = "-std=c11 -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef -Wdouble-promotion -Wcast-qual -Wvla".split()
    subprocess.run([arguments.compiler, *flags, "-Istubs", "-I" + str(component),
                    "test_ble_ccc.c", str(component / "ble_ccc_store.c"),
                    "-o", str(executable.relative_to(tests))], cwd=tests, check=True)
    subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
