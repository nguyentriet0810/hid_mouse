"""Portable host C test runner used by GitHub Actions and Windows."""
import argparse
from pathlib import Path
import subprocess
import os


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="gcc")
    arguments = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    build = project / "tests" / "build"
    build.mkdir(parents=True, exist_ok=True)
    executable = build / ("test_core.exe" if os.name == "nt" else "test_core")
    components = ("mouse_types", "mouse_input", "mouse_motion", "ota_policy")
    includes = ["-I" + str(project / "components" / item / "include") for item in components]
    sources = [project / "tests" / "test_core.c", project / "tests" / "test_ota_policy.c"]
    sources.extend(project / "components" / item / (item + ".c") for item in components[1:])
    flags = "-std=c11 -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef -Wdouble-promotion -Wcast-qual -Wvla".split()
    subprocess.run([arguments.compiler, *flags, *includes, *map(str, sources), "-o", str(executable), "-lm"], check=True)
    subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
