/* SPDX-License-Identifier: MIT */
/**
 * @file ota_update.h
 * @brief Application-facing GitHub OTA maintenance and rollback checkpoints.
 *
 * The main task owns this component. Update maintenance runs before BLE starts;
 * the running mouse later supplies health observations for an unconfirmed image.
 */
#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdbool.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "esp_err.h"
#pragma GCC diagnostic pop

/**
 * @brief Inspect boot state and optionally perform a startup maintenance update.
 *
 * @pre Board GPIO and NVS are initialized; BLE has not been started.
 * @pre Call once from the main application task during startup.
 *
 * Holding both mouse buttons for three seconds requests maintenance. A pending
 * image skips maintenance so it cannot replace itself before its health check.
 * Successful installation reboots. No newer release or no gesture returns
 * normally; missing settings and transport/validation failures return an error.
 * The current boot selection is retained unless installation fully succeeds.
 *
 * @return ESP_OK when startup may continue without an update, or an ESP-IDF
 *         error describing the failed maintenance attempt. The caller may still
 *         continue normal BLE mouse operation after a maintenance error.
 */
esp_err_t ota_update_startup(void);

/**
 * @brief Advance the first-boot health check of an OTA-installed image.
 *
 * @param healthy True only when the board memory check and BLE initialization
 *                passed, the MPU is calibrated, and the mouse loop is healthy.
 *
 * Call regularly from the owning main task. Any unhealthy observation restarts
 * the continuous ten-second confirmation interval. The overall 120-second
 * deadline is measured from ota_update_startup(), not from the last observation.
 * Confirmed or non-OTA boots require no action. A failed deadline may reboot
 * through the SDK rollback API.
 */
void ota_update_boot_health(bool healthy);

/**
 * @brief Request rollback after a fatal startup or health-check failure.
 *
 * Boot metadata is queried if startup failed before the component cached it.
 * The function is a no-op for a non-pending image. A successful rollback call
 * reboots; if the SDK cannot roll back, its error is logged and control returns.
 * Call from the owning application task, outside critical sections.
 */
void ota_update_boot_failed(void);

#endif
