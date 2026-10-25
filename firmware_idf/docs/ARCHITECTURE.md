# Runtime design

## Ownership

The ESP-IDF main task owns board I/O, MPU context, motion filter, debouncers and
button FIFO. It runs a nominal 10 ms period using xTaskDelayUntil. There is no
task for each peripheral, no application heap allocation, and no cross-task
access to the I2C device. SDK tasks own GAP/GATT callbacks.

BLE callbacks own initialization progress and update a small connection record
under a spinlock. The application takes bounded snapshots. The spinlock never
covers SDK calls or I2C. The connection generation counter lets the application
discard old button events when a BLE session changes.

## Data flow

```text
GPIO inputs -> mouse_input -> ordered button snapshots --+
                                                       +-> app -> ble_mouse -> host
I2C -> mpu6050 -> physical units -> mouse_motion --------+
```

`mpu6050` has no pointer/scroll policy. `mouse_motion` and `mouse_input` have no
ESP-IDF includes. `ble_mouse` does not know board GPIO or sensor registers.

`ota_policy` is also hardware-independent. It validates bounded manifests and
semantic versions. `ota_update` owns its private Wi-Fi/HTTPS adapter and runs only
in startup maintenance, before BLE initialization. It streams into the inactive
slot, checks SHA-256 and ESP image identity, then changes boot selection. The
normal loop supplies health checkpoints for pending-image confirmation/rollback.
See OTA.md for the N16R8 memory preset, partition layout and release workflow.

## Motion

MPU6050/MPU6500: +/-2 g, +/-500 degrees/s, DLPF 3, 100 Hz output.
WHO_AM_I 0x68 and 0x70 select explicit profiles. MPU6500 additionally requires
ACCEL_CONFIG2=0x03 for its independent 41 Hz accelerometer DLPF; that register is
never written on MPU6050. Configured ranges/filter settings are read back before
samples are exposed, and unknown identities are rejected before reset/config writes.
Collect 200 samples to estimate gyro bias and neutral attitude. Calibration
rejects excessive variance, constant rotation and implausible gravity. Combine
accelerometer roll/pitch with integrated gyroscope using a 0.5 s filter time
constant. Ignore accelerometer correction during large linear acceleration.

Subtract neutral attitude, remove a continuous deadzone, multiply by velocity
gain and elapsed time, accumulate fractional pixels, then saturate to the
signed 8-bit HID range. The result is a tilt-controlled velocity mouse.
This two-axis filter is intended for moderate tilt, not arbitrary 3D tracking.

## Inputs and failure behavior

Debounce is per key (20 ms). Left/right edges go directly to HID states, so the
host handles double-click and dragging. BOOT short release recenters; a 300 ms
hold enables vertical scrolling. No timing delay is added to normal clicks.

BLE congestion retains button snapshots and drops stale motion. Queue capacity
is 16 button states. Overflow queues all-buttons-up and waits for physical
release. A disconnect clears old states. A new session is armed only after
buttons are released. Suspend/subscription loss is treated as an inactive link.

An I2C read error suppresses that tick's movement. Three consecutive errors
release the I2C device and trigger a two-second retry. Buttons remain usable
without the MPU. Recovery recalibrates the sensor. Missing sensor never enters
a busy loop. BLE initialization has a ten-second deadline and startup errors
are logged before the main task returns.

## BLE interface

GATT services: Human Interface Device 0x1812, Device Information 0x180A, Battery
0x180F. Report ID 1, four value bytes: buttons, relative X, relative Y, wheel.
No leading report ID in the GATT characteristic payload. HID Report Reference
contains [1, input]. Normal report protocol only; no optional boot protocol.

Advertising data contains HID UUID and mouse appearance; complete device name
is in the scan response to stay inside the 31-byte advertising limit.
Pairing uses Secure Connections bonding with no-I/O Just Works; reports require
an encrypted connection, enabled notifications and a non-suspended host.
Bluetooth keys are stored by the SDK in NVS.

BAS is an explicitly configured development placeholder because this board has
no battery-sense input. It does not estimate remaining charge. PnP identity must
be configured for a production product; development source does not claim SIG
qualification or a registered vendor identity.
