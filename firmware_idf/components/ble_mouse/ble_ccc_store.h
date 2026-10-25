/* SPDX-License-Identifier: MIT */
/** @file ble_ccc_store.h
 * @brief Private NVS storage for per-bond HID/BAS notification configuration.
 *
 * The BLE adapter supplies an authenticated identity obtained from the SDK bond
 * database. This module does not authenticate peers and never stores BLE keys.
 */
#ifndef BLE_CCC_STORE_H
#define BLE_CCC_STORE_H

#include <stdint.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "esp_err.h"
#pragma GCC diagnostic pop

#define BLE_CCC_MOUSE UINT8_C(1)
#define BLE_CCC_BATTERY UINT8_C(2)
#define BLE_CCC_MASK UINT8_C(3)

typedef struct
{
    uint8_t address[6]; /**< Canonical public or static-random identity address. */
    uint8_t address_type; /**< 0 = public identity; 1 = static-random identity. */
} ble_ccc_peer_t;

/**
 * @brief Load a versioned record, setting flags to zero on every failure.
 * @return ESP_OK, ESP_ERR_NVS_NOT_FOUND, or an NVS/argument/format error.
 * @pre Caller serializes access and verifies the peer is currently bonded.
 */
esp_err_t ble_ccc_store_load(const ble_ccc_peer_t *peer, uint8_t *flags);
/**
 * @brief Commit mouse/battery notify bits, including explicit disable (zero).
 * @note Identical records do not cause another NVS set/commit. Failure does not
 * erase NVS and must be reported by the caller; live-link state can still work.
 */
esp_err_t ble_ccc_store_save(const ble_ccc_peer_t *peer, uint8_t flags);

#endif
