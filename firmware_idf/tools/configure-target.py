"""Migrate an existing local sdkconfig to the N16R8/dual-OTA hardware preset.

Only target/memory/partition/TLS settings change. Mouse tuning, Wi-Fi and other
local options are preserved. New builds already inherit sdkconfig.defaults.
"""
from pathlib import Path


def main() -> None:
    project = Path(__file__).resolve().parent.parent
    config = project / "sdkconfig"
    if not config.exists():
        print("No local sdkconfig: next IDF configure will use sdkconfig.defaults")
        return
    defaults = (project / "sdkconfig.defaults").read_text(encoding="utf-8").splitlines()
    prefixes = ("CONFIG_SPIRAM", "CONFIG_PARTITION_TABLE", "CONFIG_BOOTLOADER_APP_ROLLBACK",
                "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE", "CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC",
                "CONFIG_ESPTOOLPY_FLASHSIZE", "CONFIG_ESP_MAIN_TASK_STACK_SIZE")
    updates = {line.split("=", 1)[0]: line for line in defaults
               if line.startswith(prefixes) and "=" in line}
    for name in ("CONFIG_PARTITION_TABLE_SINGLE_APP", "CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE",
                 "CONFIG_PARTITION_TABLE_TWO_OTA", "CONFIG_SPIRAM_MODE_QUAD",
                 "CONFIG_SPIRAM_SPEED_40M", "CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC",
                 "CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC"):
        updates[name] = f"# {name} is not set"
    lines = config.read_text(encoding="utf-8").splitlines()
    retained = []
    for line in lines:
        key = line[2:].split(" ", 1)[0] if line.startswith("# CONFIG_") else line.split("=", 1)[0]
        if key not in updates:
            retained.append(line)
    config.write_text("\n".join(retained + list(updates.values())) + "\n", encoding="utf-8")
    print("Updated N16R8, Octal PSRAM and dual-OTA settings; local options preserved")


if __name__ == "__main__":
    main()
