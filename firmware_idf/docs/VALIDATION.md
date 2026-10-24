# Validation record

Date: 2026-10-25 (Asia/Bangkok).
Target: ESP32-S3-WROOM-1-N16R8, MPU6050, schematic GPIO mapping.

## Automated checks

| Check | Result |
|---|---|
| Windows host C tests | PASS, 19 test groups including OTA policy |
| Python release/partition tests | PASS, 4 tests |
| GCC path analyzer | PASS, all 9 production translation units, no diagnostics |
| ESP-IDF firmware build | PASS, ESP32-S3 image generated |
| Flash partition fit | PASS; image fits the single-app partition |
| MISRA-specific rule checker | NOT RUN; no compliance claim |
| Flash / pairing / live sensor operation | NOT RUN; hardware validation pending |
| GitHub Release packaging | PASS, version 1.1.0, binary/manifest/checksums/first-flash ZIP |
| Live Wi-Fi OTA / power-loss / rollback | NOT RUN; requires real board |

N16R8 build: 16 MiB flash, 8 MiB Octal PSRAM at 80 MHz, two 6 MiB app slots,
rollback enabled. App image is 1,395,472 bytes (about 1.33 MiB); each app slot has
about 78% free space. Build outputs remain local and are ignored by Git.

Build environment:

- Installed SDK at `C:\Espressif\frameworks\esp-idf-v5.3.1`.
- The installed SDK reports **v5.3.1-dirty**. It already has local modifications;
  this work does not modify the SDK. Freeze/audit that installation for release.
- Xtensa GCC 13.2.0, crosstool-NG esp-13.2.0_20240530.
- ESP-IDF Python 3.11.2, CMake 3.24.0, Ninja 1.11.1.
- Host compiler: MinGW GCC 6.3.0, C11.
- Application target uses IDF's GNU C17 mode and strict diagnostics:
  `-Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow
  -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef
  -Wdouble-promotion -Wcast-qual -Wvla`.
- GCC `-fanalyzer` reuses `build/compile_commands.json`; logs are in
  `build/analysis/`. This detects general defects, not MISRA compliance.

Reproduce from the firmware directory:

```powershell
.\tools\test-host.ps1 -Compiler C:\MinGW\bin\gcc.exe
python -m unittest discover -s tests -p 'test_*.py'
.\tools\idf.ps1 -Action build
& C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe .\tools\analyze.py
```

## Core test coverage

1. Configuration bounds, non-finite values, null arguments.
2. Calibration timing and zero drift with a stationary sensor.
3. Constant rotation rejects calibration.
4. Missing gravity rejects calibration (all-zero sensor data).
5. Non-finite/out-of-range samples and invalid dt are rejected.
6. Vertical tilt direction, speed limit, wheel mode, recenter.
7. Fractional pixel accumulation and horizontal direction.
8. Gyroscope bias calibration/cancellation.
9. Per-button debounce and sustained drag.
10. Ordered double-click edges survive delayed sending.
11. Disconnect clears queued events; held buttons do not fire on reconnect.
12. Short BOOT recenter and long BOOT scroll gesture.
13. Millisecond timer rollover.
14. Full FIFO forces all-buttons-up and waits for release.
15. Manifest valid identity/size/digest and optional trailing newline.
16. Reject wrong board/project/target/asset and malformed values.
17. Reject duplicate/missing/unknown keys, embedded NUL and oversized input.
18. Numeric version ordering and rejection of downgrade/equal/prerelease versions.
19. Null/boundary/overflow cases for OTA policy fields.

Python tests additionally verify digest generation, release-version validation,
image-size bounds, app alignment, no partition overlap, both OTA slots and exact
16 MiB flash bounds. See OTA.md for the publication/maintenance contract.

## Required bench tests before use/release

- Confirm rails and I2C pull-ups; verify address 0x68/0x69 and chip ID 0x68.
- Check sensor axis orientation, two-second stationary calibration and LEDs.
- Pair on the intended OS; inspect HIDS Report Map and encrypted CCC writes.
- Verify motion, deadzone, recenter, click, right-click, double-click, drag,
  scroll and simultaneous buttons at the expected connection interval.
- Turn Bluetooth off/on, restart host/device, check bonded reconnection and
  no stuck buttons. Remove stale host bonds if changing the report descriptor.
- Test unsubscribe/resubscribe, host suspend/resume, connection congestion and
  queue overflow. No old click may be replayed into a later connection.
- Unplug/reconnect MPU6050 and confirm zero unwanted movement plus recovery.
- Measure latency, current draw, battery runtime and long-run sensor drift.
- Replace the fixed battery percentage with actual measurement if needed.
- Complete a MISRA-capable analysis and review the deviation register before
  describing the production firmware as MISRA compliant.
