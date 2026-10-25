# Real MPU6050 driver host regression tests

The runner compiles the project's actual `components/mpu6050/mpu6050.c`, not a
second implementation of the driver. Minimal SDK headers and a fake register map
replace I2C, task delay and log boundaries. No board is accessed or flashed.

```powershell
python run.py --project D:\ESP32_mouse\firmware_idf --compiler C:\MinGW\bin\gcc.exe
```

Linux/CI can pass `--compiler gcc` (the default) and a relative project path.
The runner resolves that path before compiling in the fixture directory.

Full mode checks fourteen groups:

1. Valid identity 0x68 at either bus address 0x68 or 0x69; correct configured scales.
2. A stable foreign WHO_AM_I (0x71) is rejected and never receives reset/config writes.
3. A valid MPU at 0x69 is found after a foreign responding device at 0x68.
4. No responding device returns not found without retaining a handle.
5. A configuration-write transport failure propagates and releases the context.
6. Failed teardown retains the registration so later cleanup can retry.
7. Signed coherent burst conversion and unchanged output on failed sample reads.
8. Transient identity mismatch or read transport error recovers within three reads.
9. Register read-back mismatch rejects initialization and cleans up.
10. At least five configuration registers are read back on a successful setup.
11. Confirmed MPU6500 WHO_AM_I 0x70 receives its separate ACCEL_CONFIG2=0x03
    filter setup, six register readbacks and the same 2 g / 500 degrees/s scales.
12. MPU6500 accelerometer filter read-back mismatch rejects initialization.
13. MPU6500 gyro bypass-bit read-back mismatch rejects initialization.
14. Model-name accessor handles both supported IDs, NULL and unknown IDs.

Use `--baseline` for the first seven checks against the pre-diagnostic driver.
Full mode assumes the new driver retries WHO_AM_I and reads back PWR_MGMT_1,
CONFIG, SMPLRT_DIV, GYRO_CONFIG and ACCEL_CONFIG. The deliberately supported
MPU6500 profile additionally writes/reads ACCEL_CONFIG2. MPU6050 must never write
that MPU6500-only register. The context identity is cleared after deinit.
Tests do not establish electrical correctness, silicon identity or live I2C
behavior; a new hardware log is still needed for the user's reported failure.

The fixture headers are deliberately private to host tests and must never appear
in production include directories. Generated executables stay under `build/`.
