/* SPDX-License-Identifier: MIT */
#ifndef OTA_POLICY_H
#define OTA_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_PROJECT_NAME "esp_mouse"
#define OTA_BOARD_NAME "esp32s3-n16r8"
#define OTA_ASSET_NAME "esp32s3-n16r8.bin"
#define OTA_MANIFEST_NAME "esp32s3-n16r8.manifest"
#define OTA_VERSION_CAPACITY 32U
#define OTA_MANIFEST_CAPACITY 512U
#define OTA_MAX_IMAGE_SIZE UINT32_C(0x600000)

typedef struct
{
    char version[OTA_VERSION_CAPACITY];
    uint32_t image_size;
    uint8_t sha256[32];
} ota_manifest_t;

/* Strict bounded key=value format; all eight fields are required exactly once.
 * Unknown fields, NULs, duplicate keys, invalid board/version/digest are rejected.
 */
bool ota_policy_parse(const char * const data, size_t length,
                       ota_manifest_t * const manifest);
bool ota_policy_newer(const char * const candidate, const char * const current);

#endif
