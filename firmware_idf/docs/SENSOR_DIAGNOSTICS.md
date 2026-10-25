# MPU6050 / MPU6500 identity diagnosis

## Observed failure and cause

On 2026-10-25, the board repeatedly printed:

```text
mouse_app: MPU6050 unavailable: ESP_ERR_INVALID_RESPONSE; retry in 2 seconds
```

The original driver emitted that error only after a successful I2C WHO_AM_I read
whose value was not 0x68. Its generic application log hid the actual identity.
The board was reported as a GY-521/MPU6050 module; direct COM12 diagnosis showed:

```text
mpu6050: I2C 0x68 WHO_AM_I[0x75]=0x70 (expected 0x68), attempt 1/3
mpu6050: I2C 0x68 WHO_AM_I[0x75]=0x70 (expected 0x68), attempt 2/3
mpu6050: I2C 0x68 WHO_AM_I[0x75]=0x70 (expected 0x68), attempt 3/3
mpu6050: I2C 0x68 PWR_MGMT_1[0x6B]=0x01; identity rejected, no writes performed
```

0x70 is the MPU6500 WHO_AM_I identity. The module label alone was not sufficient
to select a sensor configuration. The I2C address (AD0 selects 0x68/0x69) is
separate from the WHO_AM_I value and must not be used as the chip identity.

## Version 1.1.1 behavior

- Recognizes only documented profiles: 0x68 MPU6050 and 0x70 MPU6500.
- Retries identity reads at most three times and logs both byte/address and
  transport errors. An unknown device receives no reset/configuration writes.
- Keeps +/-2 g and +/-500 degrees/s conversion (16384 LSB/g, 65.5 LSB/dps).
- MPU6500 sets its independent ACCEL_CONFIG2 register 0x1D to 0x03, enabling
  the 41 Hz accelerometer DLPF. MPU6050 does not receive this write.
- Verifies power, sample divider, filter and range register read-backs. MPU6500
  additionally verifies accel DLPF and gyro filter-bypass bits.
- Allows MPU6500 settling before bias calibration, and logs the detected model.
- Preserves I2C probe errors instead of misreporting a timeout as a missing sensor.

The component/API retain their mpu6050 names for compatibility; the model accessor
and identity field make the selected profile explicit. No generic accept-any-ID
mode or reserved-bit masking is used to bypass identity validation.

## Live validation

The actual partition table and OTA metadata were read before flashing: ota_0
was at 0x20000, 6 MiB, sequence 1 with valid state. Only that application slot
was written. Bootloader, partition table and NVS were preserved.

After flashing the MPU6500-enabled application and leaving the sensor still, repeated
identity/configuration failures were absent during extended serial observation.
A user-operated short BOOT press produced:

```text
I (159829) mouse_app: Neutral position recentered
```

That branch requires motion.ready, which is reached only after the stationary
calibration window passes. This confirms the sensor/configuration/calibration
path. It does not prove BLE pairing or cursor behavior: the same serial session
still contained `ble_mouse: Pairing failed: 0x66`, tracked as a separate issue.

## Reproduce software checks

```powershell
python tests/mpu_driver/run.py --project . --compiler C:\MinGW\bin\gcc.exe
```

The fourteen groups compile the real driver against private fake I2C/RTOS/log
boundaries. They cover both bus addresses, supported/unknown IDs, retries,
cleanup, transport errors, model-specific filtering, read-back failures and
signed sample scaling. They run in CI without attached hardware.

## Board-operation coordination

The user operates hardware BOOT/RESET. Announce the exact firmware and wait for
readiness before future flashing. For manual download mode, hold BOOT, press and
release RESET, then release BOOT. An esptool `--before no_reset` connection can
then use the manually selected bootloader. Do not erase NVS or reset partition
metadata as part of a sensor-driver diagnosis. Close the monitor before flashing.

Reference: [TDK MPU6500 register map](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6500-Register-Map2.pdf),
registers 0x1D (ACCEL_CONFIG2) and 0x75 (WHO_AM_I).
