r"""Compile the real MPU6050/MPU6500 adapter using fake SDK headers/register I/O.

Example:
python run.py --project D:\ESP32_mouse\firmware_idf --compiler C:\MinGW\bin\gcc.exe
Use --baseline to run only checks supported by the original unmodified driver.
"""
from pathlib import Path
import argparse
import os
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--compiler", default="gcc")
    parser.add_argument("--baseline", action="store_true")
    arguments = parser.parse_args()
    project = arguments.project.resolve()
    tests = Path(__file__).resolve().parent
    build = tests / "build"
    build.mkdir(exist_ok=True)
    executable = build / ("test_mpu6050.exe" if os.name == "nt" else "test_mpu6050")
    includes = [Path("stubs"), project / "components" / "mpu6050" / "include",
                project / "components" / "mouse_types" / "include"]
    flags = "-std=c11 -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef -Wdouble-promotion -Wcast-qual -Wvla".split()
    if arguments.baseline:
        flags.append("-DTEST_BASELINE_DRIVER")
    sources = [Path("test_mpu6050.c"), project / "components" / "mpu6050" / "mpu6050.c"]
    subprocess.run([arguments.compiler, *flags, *["-I" + str(path) for path in includes],
                    *map(str, sources), "-o", str(executable.relative_to(tests)), "-lm"],
                   cwd=tests, check=True)
    subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
