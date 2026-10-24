/* SPDX-License-Identifier: MIT */
#ifndef OTA_NETWORK_H
#define OTA_NETWORK_H

#include <stddef.h>
#include "ota_update.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "esp_http_client.h"
#pragma GCC diagnostic pop

/* Single main-task owner; maintenance mode only, before BLE initialization. */
esp_err_t ota_network_connect(void);
void ota_network_disconnect(void);
esp_err_t ota_network_open_https(const char * const url,
                                 esp_http_client_handle_t * const client);
esp_err_t ota_network_fetch(const char * const url, char * const output,
                             size_t capacity, size_t * const length);

#endif
