# ESP32-S3 N16R8 and GitHub Release OTA

## Hardware and flash layout

Supported module: **ESP32-S3-WROOM-1-N16R8**, with 16 MiB flash and 8 MiB
Octal PSRAM. Flash uses DIO at 80 MHz. Octal PSRAM uses 80 MHz, startup
initialization and memory test. The board adapter verifies both memory sizes.
TLS allocations may use PSRAM; 64 KiB of internal memory is reserved.

| Partition | Offset | Size | Purpose |
|---|---:|---:|---|
| nvs | 0x009000 | 24 KiB | BLE bonds and OTA Wi-Fi settings |
| otadata | 0x00F000 | 8 KiB | Redundant boot selection/rollback metadata |
| phy_init | 0x011000 | 4 KiB | PHY initialization |
| ota_0 | 0x020000 | 6 MiB | First app slot |
| ota_1 | 0x620000 | 6 MiB | Second app slot |
| storage | 0xC20000 | 3904 KiB | Reserved SPIFFS data; not mounted |
| coredump | 0xFF0000 | 64 KiB | Reserved; flash coredumps not enabled |

App offsets are aligned to 64 KiB. The last partition ends at 0x1000000.
There is no factory app; a fresh install boots ota_0. PSRAM is RAM, not a
flash partition. Later OTA updates alternate between the two app slots.

**Migration from the old single-app layout requires one full USB flash of the
new bootloader, partition table, OTA metadata and app. An app-only OTA cannot
perform this migration.** NVS keeps its previous offset and size.

For the previous local sdkconfig:

```powershell
python .\tools\configure-target.py
.\tools\idf.ps1 -Action reconfigure
.\tools\idf.ps1 -Action build
.\tools\idf.ps1 -Action flash -Port YOUR_PORT
```

The migration helper changes only hardware/OTA settings and preserves mouse
tuning and Wi-Fi options. Fresh builds use sdkconfig.defaults directly.

## First Wi-Fi setup and update gesture

1. Open **Component config > GitHub OTA update** in menuconfig.
2. Set the initial 2.4 GHz Wi-Fi SSID/password in local `sdkconfig`, which Git
   ignores. Build and USB-flash once. Do not publish this credential-bearing bin.
3. Hold **both GPIO10 and GPIO11 mouse buttons**, press/release RESET and keep
   holding for three seconds. BOOT/GPIO0 is not used for entering OTA.
4. The first maintenance attempt saves credentials to NVS namespace `ota_net`.
   Subsequent CI-built updates have blank credentials and reuse those in NVS.
5. To replace the network, flash a local build containing new credentials with
   **Replace saved Wi-Fi credentials** enabled, then enter maintenance again.

Passwords are never logged. NVS encryption/secure boot are not enabled; local
bootstrap binaries still contain the configured credentials. The release tool
refuses to package a build with a non-empty Wi-Fi SSID/password.

Maintenance runs before BLE starts. It connects to Wi-Fi, synchronizes time
for certificate validity checks and fetches the latest published release:

```text
https://github.com/nguyentriet0810/hid_mouse/releases/latest/download/esp32s3-n16r8.manifest
```

No GitHub token is embedded; only public releases are supported. Missing Wi-Fi,
NTP/network failure, absent release or a non-newer release returns to the current
BLE mouse. Wi-Fi resources are then released. Normal operation never turns on
Wi-Fi or polls for updates in the background.

## Manifest, integrity and rollback

The packaging tool writes a strict ASCII key=value manifest with eight fields:
`schema`, `project`, `target`, `board`, `version`, `size`, `sha256`, `asset`.
Identity is `esp_mouse`, `esp32s3`, `esp32s3-n16r8`; the asset is
`esp32s3-n16r8.bin`. Size and SHA-256 come from the actual binary. Unknown or
duplicate fields, malformed values, wrong identity and invalid sizes are refused.
Only plain major.minor.patch versions are supported; no equal versions,
downgrades, prereleases or path fragments are accepted.

The firmware derives the asset URL from the trusted configured repository and
the checked version tag `vX.Y.Z`; the manifest cannot supply a download URL.
Redirects are limited and must remain HTTPS. CA-bundle, hostname and certificate
time verification remain enabled. SHA-256 is computed while writing the inactive
slot. Exact byte count, ESP application project/version, chip ID and ESP image
validation must pass before boot selection changes. Failed/interrupted downloads
leave the current image selected. Reads have timeouts; streaming has a 180 s
deadline. SHA-256 provides integrity; publisher authenticity relies on HTTPS
and GitHub repository access control. Firmware signatures are not claimed.

After OTA, the new image is pending verification. Memory/board checks and BLE
initialization must succeed, MPU6050 must calibrate, and the loop must remain
healthy for ten continuous seconds before confirmation. No paired host is
required. Fatal startup errors request rollback; failure to become healthy in
120 seconds also requests rollback. Reset/crash before confirmation lets the
bootloader roll back on the next boot. Pending images cannot install updates.
Keep the sensor still on first boot so calibration can complete.

## Release workflow

`version.txt` controls the ESP app version. For the next version, change it,
commit/push, then push the matching tag (for example `v1.1.1` for `1.1.1`).
The workflow runs strict C/partition/package tests and builds in
`espressif/idf:v5.3.1`. Branch pushes create CI artifacts. Version tags also
publish a GitHub Release with:

- `esp32s3-n16r8.bin`: application-only OTA image.
- `esp32s3-n16r8.manifest`: identity/version/size/SHA-256.
- `SHA256SUMS`: application and manifest checksums.
- `esp32s3-n16r8-first-flash.zip`: bootloader, partition table, erased OTA
  metadata, app and instructions for initial installation/layout migration.
- `partitions.csv`: the flash layout.

Existing published releases are not overwritten on workflow reruns. Pushing
source alone does not publish a release or create a version tag.

To package a clean local build without publishing:

```powershell
python .\tools\make-release.py
```

Outputs go into ignored `release/`. Build products, local sdkconfig, credentials,
logs and IDE state are excluded from the Git commit.

## Hardware validation pending

Host tests cover policy parsing, version order, flash geometry and packaging.
First USB flash, PSRAM detection, Wi-Fi setup, real GitHub redirects, interrupted
downloads, incorrect checksums and automatic rollback still require bench tests.

References: [ESP-IDF 5.3.1 OTA](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-reference/system/ota.html),
[GitHub Releases](https://docs.github.com/en/rest/releases/releases).
