/* SPDX-License-Identifier: MIT */
/** @file ble_ccc_store.c
 * @brief Bounded per-identity CCC records; SDK/NVS boundary, see docs/MISRA.md.
 */
#include "ble_ccc_store.h"
#include <stddef.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "nvs.h"
#pragma GCC diagnostic pop

#define RECORD_VERSION UINT8_C(0xA0)
#define RECORD_VERSION_MASK UINT8_C(0xFC)
#define PEER_KEY_BYTES 14U

/** Encode one address type plus twelve hex digits within NVS's 15-char limit. */
static void make_key(const ble_ccc_peer_t * const peer, char key[PEER_KEY_BYTES])
{
    static const char digits[] = "0123456789abcdef";
    key[0] = (peer->address_type == 0U) ? 'p' : 'r';
    for (size_t index = 0U; index < sizeof(peer->address); index++)
    {
        key[1U + (2U * index)] = digits[peer->address[index] >> 4U];
        key[2U + (2U * index)] = digits[peer->address[index] & UINT8_C(0x0F)];
    }
    key[PEER_KEY_BYTES - 1U] = '\0';
}

esp_err_t ble_ccc_store_load(const ble_ccc_peer_t * const peer, uint8_t * const flags)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (flags != NULL)
    {
        *flags = 0U; /* Never let corrupt/missing persistence enable notifications. */
        if ((peer != NULL) && (peer->address_type <= 1U))
        {
            nvs_handle_t handle = 0U;
            char key[PEER_KEY_BYTES];
            make_key(peer, key);
            result = nvs_open("hid_ccc", NVS_READONLY, &handle);
            if (result == ESP_OK)
            {
                uint8_t record = 0U;
                result = nvs_get_u8(handle, key, &record);
                nvs_close(handle);
                if (result == ESP_OK)
                {
                    if ((record & RECORD_VERSION_MASK) == RECORD_VERSION)
                    {
                        *flags = record & BLE_CCC_MASK;
                    }
                    else
                    {
                        result = ESP_ERR_INVALID_RESPONSE;
                    }
                }
            }
        }
    }
    return result;
}

esp_err_t ble_ccc_store_save(const ble_ccc_peer_t * const peer, uint8_t flags)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if ((peer != NULL) && (peer->address_type <= 1U) &&
        ((flags & (uint8_t)(~BLE_CCC_MASK)) == 0U))
    {
        nvs_handle_t handle = 0U;
        char key[PEER_KEY_BYTES];
        make_key(peer, key);
        result = nvs_open("hid_ccc", NVS_READWRITE, &handle);
        if (result == ESP_OK)
        {
            uint8_t previous = 0U;
            const uint8_t record = RECORD_VERSION | flags;
            result = nvs_get_u8(handle, key, &previous);
            if ((result == ESP_ERR_NVS_NOT_FOUND) ||
                ((result == ESP_OK) && (previous != record)))
            {
                result = nvs_set_u8(handle, key, record);
                if (result == ESP_OK)
                {
                    result = nvs_commit(handle);
                }
            }
            nvs_close(handle);
        }
    }
    return result;
}
