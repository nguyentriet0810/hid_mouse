/* SPDX-License-Identifier: MIT */
/**
 * @file ota_policy.h
 * @brief Hardware-independent validation rules for the GitHub OTA manifest.
 *
 * The parser owns no memory and performs no I/O. It establishes board identity,
 * size and version constraints before the transport can write an inactive slot.
 * Actual downloaded bytes must still be checked against the decoded SHA-256.
 */
#ifndef OTA_POLICY_H
#define OTA_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Release identity and fixed asset names shared with the packaging tool. */
#define OTA_PROJECT_NAME "esp_mouse"
#define OTA_BOARD_NAME "esp32s3-n16r8"
#define OTA_ASSET_NAME "esp32s3-n16r8.bin"
#define OTA_MANIFEST_NAME "esp32s3-n16r8.manifest"
/** Capacities include the version terminator; manifest length must be smaller. */
#define OTA_VERSION_CAPACITY 32U
#define OTA_MANIFEST_CAPACITY 512U
/** Maximum image bytes accepted by either 6 MiB app slot. */
#define OTA_MAX_IMAGE_SIZE UINT32_C(0x600000)

/** Validated metadata only; no server-supplied download URL is retained. */
typedef struct
{
    char version[OTA_VERSION_CAPACITY]; /**< NUL-terminated major.minor.patch. */
    uint32_t image_size;                /**< Expected application bytes, not slot size. */
    uint8_t sha256[32];                 /**< Binary digest decoded from 64 hex digits. */
} ota_manifest_t;

/**
 * @brief Parse all eight mandatory key=value fields without allocating memory.
 * @param[in] data Buffer of exactly length readable bytes; a terminator is not required.
 * @param[in] length Manifest byte count, excluding any optional C terminator.
 * @param[out] manifest Written only after the entire manifest has been accepted.
 * @return true for a complete supported manifest; false for NULL pointers,
 *         invalid bounds, unknown/duplicate/missing keys or invalid values.
 *
 * LF and CRLF line endings are accepted. The final newline is optional; embedded
 * NULs are rejected. On failure the caller's previous manifest remains intact.
 */
bool ota_policy_parse(const char * const data, size_t length,
                       ota_manifest_t * const manifest);

/**
 * @brief Compare numeric major/minor/patch components, rather than text order.
 * @param[in] candidate Proposed release version as a NUL-terminated string.
 * @param[in] current Running application version as a NUL-terminated string.
 * @return true only for valid versions where candidate is strictly greater.
 *
 * Leading zeroes, prerelease/build suffixes and uint32 component overflow are
 * unsupported. NULL, equal, invalid or older versions return false.
 */
bool ota_policy_newer(const char * const candidate, const char * const current);

#endif
