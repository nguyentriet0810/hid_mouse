/* SPDX-License-Identifier: MIT */
#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdbool.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "esp_err.h"
#pragma GCC diagnostic pop

/* Board and NVS must be initialized. Call before starting BLE.
 * Both mouse buttons held for 3 seconds enter the bounded maintenance flow.
 * A successful update reboots; missing configuration/update/failure returns.
 */
esp_err_t ota_update_startup(void);

/* Call from the owning application task. Ten continuously healthy seconds
 * validate a pending image; failure to become healthy in 120 seconds rolls back.
 * healthy means BLE initialized, memory checked, MPU calibrated and loop alive.
 */
void ota_update_boot_health(bool healthy);
void ota_update_boot_failed(void);

#endif
