/* SPDX-License-Identifier: MIT */
#ifndef BLE_MOUSE_H
#define BLE_MOUSE_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "esp_err.h"
#pragma GCC diagnostic pop
#include "mouse_types.h"

typedef struct
{
    bool connected;
    bool ready;
    uint32_t generation;
} ble_mouse_status_t;

/* Requires initialized NVS. Initialize once, then send from one task only. */
esp_err_t ble_mouse_init(void);
ble_mouse_status_t ble_mouse_status(void);
esp_err_t ble_mouse_send(const mouse_report_t * const report);

#endif
