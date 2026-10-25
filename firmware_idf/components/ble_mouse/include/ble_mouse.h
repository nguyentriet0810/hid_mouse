/* SPDX-License-Identifier: MIT */
/**
 * @file ble_mouse.h
 * @brief BLE HID transport for a three-button relative mouse with a wheel.
 *
 * The application supplies logical reports; this component owns GAP/GATT,
 * pairing state and explicit four-byte wire serialization.
 */
#ifndef BLE_MOUSE_H
#define BLE_MOUSE_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "esp_err.h"
#pragma GCC diagnostic pop
#include "mouse_types.h"

typedef struct
{
    /** A physical BLE connection exists; security/subscription may be pending. */
    bool connected;
    /** Encrypted, subscribed, connected and not suspended by the host. */
    bool ready;
    /** Changes on connection/disconnection so old button events can be dropped. */
    uint32_t generation;
} ble_mouse_status_t;

/**
 * @brief Initialize BLE services/security and request connectable advertising.
 * @pre NVS is initialized; no other component owns the Bluetooth stack.
 * @return ESP_OK after services and advertising data are configured and the
 *         advertising request is accepted; otherwise a configuration/SDK error
 *         or a ten-second initialization timeout.
 * @note Initialize once. Actual advertising completion is reported by GAP.
 */
esp_err_t ble_mouse_init(void);

/**
 * @brief Take a synchronized snapshot of the current BLE session.
 * @return Connection/readiness flags and a session-generation counter.
 * @note ready does not promise available transmit capacity; send can still
 *       fail because of congestion or a link change after this snapshot.
 */
ble_mouse_status_t ble_mouse_status(void);

/**
 * @brief Submit one relative mouse report to the subscribed host.
 * @param report Buttons plus signed X/Y/wheel displacements for this report.
 * @return ESP_OK when the SDK accepts the notification, not a host receipt
 *         acknowledgment; invalid argument/state or SDK error otherwise.
 * @note Call from one application task. Retry queued button edges after failure;
 *       do not replay stale relative movement. The report is serialized here.
 */
esp_err_t ble_mouse_send(const mouse_report_t * const report);

#endif
