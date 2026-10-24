"""Build GitHub Release assets from a validated N16R8 ESP-IDF build.

No network/publishing occurs. Release binaries must not embed Wi-Fi credentials.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import zipfile

ASSET = "esp32s3-n16r8.bin"
MANIFEST = "esp32s3-n16r8.manifest"
VERSION = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\Z")


def manifest_text(version: str, image: bytes) -> str:
    if not VERSION.fullmatch(version) or any(int(part) > 0xFFFFFFFF for part in version.split(".")):
        raise ValueError("Version must be a plain major.minor.patch with uint32 components")
    if len(version) >= 32 or not 0 < len(image) <= 0x600000:
        raise ValueError("Version or image size exceeds the device limits")
    return ("schema=1\nproject=esp_mouse\ntarget=esp32s3\nboard=esp32s3-n16r8\n"
            f"version={version}\nsize={len(image)}\nsha256={hashlib.sha256(image).hexdigest()}\n"
            f"asset={ASSET}\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag", help="Optional Git tag, required to match version.txt")
    args = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    version = (project / "version.txt").read_text(encoding="utf-8").strip()
    if args.tag is not None and args.tag != "v" + version:
        raise SystemExit("Tag must equal v" + version + "; update version.txt before tagging")
    build = project / "build"
    description = json.loads((build / "project_description.json").read_text(encoding="utf-8"))
    if description.get("target") != "esp32s3" or description.get("project_version") != version:
        raise SystemExit("Build target/version mismatch; rebuild before packaging")
    configuration = (project / "sdkconfig").read_text(encoding="utf-8")
    required = ("CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y", "CONFIG_SPIRAM_MODE_OCT=y",
                "CONFIG_PARTITION_TABLE_CUSTOM=y", "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y")
    if any(setting not in configuration.splitlines() for setting in required):
        raise SystemExit("Release requires N16R8, Octal PSRAM, custom partitions and rollback")
    for setting in ("CONFIG_MOUSE_OTA_WIFI_SSID", "CONFIG_MOUSE_OTA_WIFI_PASSWORD"):
        match = re.search(r"^" + setting + r"=(.*)$", configuration, re.MULTILINE)
        if match and match.group(1).strip() != '""':
            raise SystemExit("Refusing to package credentials: use a clean release sdkconfig")
    image = (build / "esp_mouse.bin").read_bytes()
    text = manifest_text(version, image)
    release = project / "release"
    release.mkdir(exist_ok=True)
    (release / ASSET).write_bytes(image)
    (release / MANIFEST).write_text(text, encoding="ascii", newline="\n")
    (release / "SHA256SUMS").write_text(
        f"{hashlib.sha256(image).hexdigest()}  {ASSET}\n"
        f"{hashlib.sha256(text.encode('ascii')).hexdigest()}  {MANIFEST}\n", encoding="ascii")
    # An initial install needs the new bootloader/table too; OTA uses the .bin only.
    with zipfile.ZipFile(release / "esp32s3-n16r8-first-flash.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        archive.write(build / "bootloader" / "bootloader.bin", "bootloader.bin")
        archive.write(build / "partition_table" / "partition-table.bin", "partition-table.bin")
        archive.writestr("ota_data_initial.bin", bytes([0xFF]) * 0x2000)
        archive.writestr(ASSET, image)
        archive.writestr("flash_args", "--flash_mode dio --flash_freq 80m --flash_size 16MB\n"
                         "0x0 bootloader.bin\n0x8000 partition-table.bin\n"
                         "0xF000 ota_data_initial.bin\n0x20000 " + ASSET + "\n")
        archive.writestr("README.txt", "First install or partition-layout migration only.\n"
                         "With esptool installed, run from the extracted directory:\n"
                         "python -m esptool --chip esp32s3 --port YOUR_PORT write_flash @flash_args\n"
                         "This resets OTA boot metadata, preserves NVS, and enables the N16R8 layout.\n"
                         "For later OTA, use the .bin and .manifest release assets.\n")
    shutil.copyfile(project / "partitions.csv", release / "partitions.csv")
    print(f"Prepared {version}: {len(image)} bytes, SHA-256 {hashlib.sha256(image).hexdigest()}")
    print(f"Assets: {release}")


if __name__ == "__main__":
    main()
