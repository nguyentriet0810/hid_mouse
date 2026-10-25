/* SPDX-License-Identifier: MIT */
/**
 * @file ota_network.h
 * @brief Private Wi-Fi and bounded HTTPS adapter for startup OTA maintenance.
 *
 * This interface is deliberately private to ota_update. Calls must be serialized
 * by the main task; internal HTTP redirect state is not designed for concurrent
 * clients. The caller owns every successfully returned HTTP client handle.
 */
#ifndef OTA_NETWORK_H
#define OTA_NETWORK_H

#include <stddef.h>
#include "ota_update.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "esp_http_client.h"
#pragma GCC diagnostic pop

/**
 * @brief Load saved/bootstrap credentials, connect Wi-Fi and synchronize time.
 * @pre NVS is initialized and normal BLE operation has not started.
 * @return ESP_OK after an IP address and NTP time are available; otherwise an
 *         ESP-IDF error. Connection and time waits are each bounded to 30 s.
 * @note Call ota_network_disconnect() even after an error to release any
 *       resources created before the failed initialization stage.
 */
esp_err_t ota_network_connect(void);

/**
 * @brief Release only the Wi-Fi/SNTP/event resources acquired by this adapter.
 * @note Safe after partial initialization; cleanup errors are logged. This does
 *       not erase persisted Wi-Fi credentials or application NVS contents.
 */
void ota_network_disconnect(void);

/**
 * @brief Open an HTTPS response body after validating bounded redirects.
 * @param url NUL-terminated HTTPS URL, valid for the duration of this call.
 * @param[out] client Receives the open client on success; the caller must call
 *                    esp_http_client_cleanup() after consuming the body.
 * @return ESP_OK only for HTTP 200; invalid URLs, unsafe redirects, exhausted
 *         attempts or transport failures return an error. Failures after client
 *         allocation clean up the handle and set *client to NULL.
 * @note Per-operation network timeout is 15 s. At most six requests are tried
 *       (the original URL and up to five redirects). TLS checks remain enabled.
 */
esp_err_t ota_network_open_https(const char * const url,
                                 esp_http_client_handle_t * const client);

/**
 * @brief Fetch a small, complete HTTPS body into a caller-owned text buffer.
 * @param url NUL-terminated HTTPS URL.
 * @param[out] output Writable buffer that remains alive for this call.
 * @param capacity Buffer size in bytes, greater than one and at most INT_MAX.
 * @param[out] length Receives body bytes, excluding the appended NUL, on success.
 * @return ESP_OK for a complete response fitting the buffer; otherwise an
 *         ESP-IDF error. Output content/length must not be used after an error.
 * @note The bounded read reserves one byte for NUL. The body-reading phase has
 *       a 30 s deadline in addition to request/redirect connection timeouts.
 */
esp_err_t ota_network_fetch(const char * const url, char * const output,
                             size_t capacity, size_t * const length);

#endif
