# BLE/HID bench diagnosis

## Current evidence

The reported startup log identifies the MPU6500 at 0x68 and completes stationary
calibration at 9057 ms. BLE authentication succeeds at 4065 ms and again at
26055 ms. A disconnection at 21821 ms reports 0x13 (the SDK's HCI_ERR_PEER_USER).
That reason alone does not identify why the host chose to disconnect.

The phone currently reports a connection but pointer/button input does not work.
There is no `Mouse notifications enabled` line in the supplied log. This is a
missing observation, not proof that a CCC write never occurred outside the
captured interval. Successful authentication alone does not mean HID is ready.

A passive pyserial capture of COM12 also encountered the same ClearCommError
as the user's IDF monitor. Windows subsequently enumerated COM12 with status OK.
No panic/backtrace or reboot banner was captured. USB failure therefore remains
unclassified; firmware crash, power/reset and USB transport must be distinguished
using further evidence. Do not declare a laptop or firmware root cause yet.

The authorized application-only flash of diagnostic version 1.1.2 subsequently
passed esptool's hash verification. A stationary startup calibrated normally.
With the existing phone pairing, the application stayed alive with a stable
internal heap, `encrypted=1 subscribed=0 ready=0`; generated motion increased
to 234 ticks while no report could be queued. The phone only read battery level
(0x003c) and wrote the battery CCC (0x003d).

After the user forgot the device on the phone and paired again, the phone read
HID information (0x002a), the Report Map (0x002c, including long-read offsets),
the Report Reference (0x0032), and wrote the mouse CCC (0x0031). The log then
reported `Mouse notifications enabled` and `ready=1`. At 111101 ms the counters
showed `movement=779 accepted=550 rejected=0`; the user confirmed the mouse
worked. Initially this suggested stale pairing/GATT cache state. A retained-bond
reconnect then reproduced the failure: motion ticks rose from 779 to 1111 while
accepted reports remained at 550, and `subscribed=0 ready=0`. The user confirmed
that the mouse stopped again. The firmware's disconnect handler cleared CCC and
required another mouse CCC write that the bonded host did not perform.

This is a firmware reconnection defect. Bluetooth Core, Vol 3, Part G, section
[3.3.3.3](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/generic-attribute-profile--gatt-.html)
requires per-client CCC persistence across bonded connections. Deleting the
pairing temporarily hid this defect. Laptop behavior remains unverified, and
no spontaneous reset/panic was observed in this mouse/reconnect sequence.

## Version 1.1.3 correction

The private `ble_ccc_store` adapter stores versioned mouse/battery notify bits in
the separate `hid_ccc` NVS namespace. Keys distinguish public and static-random
identity addresses. It never stores an application copy of pairing keys.

- Authenticate the active peer and resolve its identity from the SDK bond
  database, preferring PID identity data when present. No last-host fallback.
- Restore only that bond's settings. Fresh key exchange resets the association
  to defaults; explicit CCC writes already accepted on this link take precedence.
- Persist notification enable and disable. Unchanged records do not cause a new
  NVS set/commit. Storage errors are logged and failed loads leave notify off.
- Disconnect clears live readiness/shared CCC attributes but retains the NVS
  record. Another peer cannot inherit the preceding host's notify settings.

The static bond lookup buffer avoids copying the entire bond table onto BTC's
small stack. Callbacks own the peer state and persistence; the main task only
reads the locked connection snapshot. SDK/NVS calls execute outside the spinlock.
Orphan CCC records are never used without a current SDK bond, and fresh keys
reset that association. NVS is not erased on a storage failure.

The HID Report Map, attribute layout/handles and permissions are unchanged.
The first upgrade from firmware without CCC persistence requires one fresh
pairing to establish the record; unknown earlier subscriptions are not guessed.

Validation: 12 real CCC-store fault-injection test groups, 19 core groups,
14 MPU driver groups, 4 Python tests, strict IDF build and analyzer over all
10 production C units passed. Application-only flashing passed hash verification.
Fresh pairing worked (`bonded=1 fresh_keys=1` followed by the mouse CCC write).
Retained-bond reconnect then logged `bonded=1 fresh_keys=0 mouse=1 battery=1`,
without a mouse CCC write; readiness returned and reports increased past 2200
with no send errors. The user confirmed the mouse continued working.
After a hardware RESET without removing the phone bond, startup authenticated
the saved bond and logged `fresh_keys=0 mouse=1 battery=1` at 4153 ms. Readiness
returned, accepted reports increased and the user confirmed mouse operation.
This verifies restoration from NVS across a complete application restart.
Windows, IRK/address rotation, multiple-host radio behavior and long-run stress
remain unverified.

## Firmware instrumentation

Version 1.1.2 adds a boot version/reset-reason line and GATT metadata logs for
connection, MTU, attribute reads/writes, HID control and connection parameters.
The input Report Map and all attribute handles/permissions are unchanged.
AUTO_RSP remains responsible for GATT responses; diagnostic reads do not send
an additional response. Bond keys and application NVS are not erased.

Enable `ESP Mouse -> Periodic BLE/HID runtime diagnostics` in menuconfig
(`CONFIG_MOUSE_BLE_DIAGNOSTICS=y`) for the bench image. This option defaults off.
Every ten seconds the application logs:

- `connected`, `encrypted`, `subscribed`, `suspended`, `congested`, `ready`:
  one synchronized snapshot of the transport state.
- `imu`, `calibrated`, `keys`: sensor state and currently debounced input bits.
- `movement`: cumulative loop iterations that produced nonzero relative motion.
- `accepted`: reports accepted into the SDK transmit path; not host receipt.
- `rejected`: attempted sends refused by the component/SDK, including congestion.
- `internal_heap`, `min_internal_heap`: internal free memory and low-water mark,
  in bytes, rather than the much larger PSRAM pool.
- `reset`: the reason associated with the current boot, repeated so it can be
  captured even when the USB monitor misses initial startup output.

Counters belong to the main task, span reconnects within one boot and wrap as
unsigned 32-bit values. They do not control mouse behavior. BLE callback logs
include attribute metadata only and do not print pairing keys or credentials.

## Interpret the next capture

| Observation | Next area to inspect |
| --- | --- |
| `connected=1 encrypted=1 subscribed=0` | Host discovery, input CCC write, cached GATT/bond state |
| `suspended=1` | HID control point suspend/resume |
| `ready=1`, movement increases, accepted stays zero | Send errors/backpressure and button queue |
| `ready=1`, accepted increases, cursor still absent | Host interpretation, notification contents and Report Map |
| `calibrated=0` | Keep still for calibration; inspect sensor input if retries persist |
| New boot log or uptime restarts | Reset reason; preserve any panic/backtrace before reset |
| COM disappears without a new boot log | Reopen COM; correlate reset reason and USB/power observations |

`ready` deliberately excludes congestion: temporary backpressure blocks sending
without disarming the current input session. A stable heap is supporting evidence
only; it does not rule out corruption or prove a healthy radio link.

## Hardware procedure

Tell the user exactly which image/partition will be written and wait for their
confirmation that hardware BOOT/RESET is ready before every flash. The known
board partition table has ota_0 at 0x20000; confirm the active slot if OTA boot
selection has changed. An application-only write preserves bootloader, partition
table and NVS. Do not erase NVS merely to hide a reconnection failure.

After flashing, capture a full stationary startup, connect one host, tilt beyond
the configured six-degree deadzone, and press/release both physical buttons.
Then disconnect/reconnect the same host and compare the flags and counters.
Preserve the first failure log before removing any saved pairing. Tests on one
phone do not establish Windows compatibility or long-run stability.

Version 1.1.2 was instrumentation only. Version 1.1.3 contains the per-bond CCC
correction above. The bench image keeps periodic diagnostics enabled; the option
defaults off for normal builds. USB transport failures and laptop compatibility
must not be declared fixed solely from the successful phone reconnect test.
