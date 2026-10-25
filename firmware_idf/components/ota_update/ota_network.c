/* SPDX-License-Identifier: MIT */
/**
 * @file ota_network.c
 * @brief Private maintenance-mode networking with persistent Wi-Fi credentials,
 *        certificate-validated HTTPS, explicit redirects and bounded body reads.
 *
 * The main task serializes these APIs. Wi-Fi event callbacks only request bounded
 * reconnects and signal an event group; they never fetch firmware or write flash.
 */
#include "ota_network.h"
#include <stdint.h>
#include <limits.h>
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "sdkconfig.h"
#pragma GCC diagnostic pop

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_RETRIES UINT8_C(5)

static const char * const TAG = "ota_network";
/* Statically backed event bits carry connection outcome from the ESP event task
 * to the waiting main task; IP_EVENT_STA_GOT_IP is the usable-network checkpoint. */
static StaticEventGroup_t s_event_storage;
static EventGroupHandle_t s_events = NULL;
static esp_netif_t *s_station = NULL;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
/* Track successful acquisition stages so partial initialization can use the
 * same teardown path without releasing objects the adapter never acquired. */
static bool s_event_loop = false;
static bool s_wifi_registered = false;
static bool s_ip_registered = false;
static bool s_wifi_initialized = false;
static bool s_wifi_started = false;
static bool s_sntp_initialized = false;
/* Callback-owned attempt count; initialized before Wi-Fi starts and never reset
 * on a reconnect, keeping the maintenance attempt bounded across disconnects. */
static uint8_t s_retries = 0U;
/* The synchronous HTTP header callback describes only the current response.
 * open_https clears this before every request to avoid trusting a stale header. */
static bool s_redirect_safe = false;

/** @brief Retain cleanup diagnostics while allowing the remaining cleanup steps. */
static void warn_result(const char * const operation, esp_err_t result)
{
    if (result != ESP_OK)
    {
        ESP_LOGW(TAG, "%s: %s", operation, esp_err_to_name(result));
    }
}

/**
 * @brief Bridge Wi-Fi/IP events to finite retries and the main task's wait bits.
 * @note Runs in the SDK event task. Handler removal precedes event-group deletion
 *       during teardown, and normal mouse operation does not register it.
 */
static void wifi_callback(void *argument, esp_event_base_t base,
                           int32_t event, void *event_data)
{
    (void)argument;
    (void)event_data;
    if ((base == WIFI_EVENT) &&
        ((event == WIFI_EVENT_STA_START) || (event == WIFI_EVENT_STA_DISCONNECTED)))
    {
        if (s_retries < WIFI_RETRIES)
        {
            ++s_retries;
            if (esp_wifi_connect() != ESP_OK)
            {
                (void)xEventGroupSetBits(s_events, WIFI_FAILED_BIT);
            }
        }
        else
        {
            (void)xEventGroupSetBits(s_events, WIFI_FAILED_BIT);
        }
    }
    else if ((base == IP_EVENT) && (event == IP_EVENT_STA_GOT_IP))
    {
        (void)xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
    }
    else
    {
        /* Other network events are not used. */
    }
}

/**
 * @brief Load NVS credentials or persist bootstrap settings from local sdkconfig.
 * @param[out] configuration Zero-initialized SDK Wi-Fi configuration to populate.
 * @return ESP_OK only if the SSID/password fit the SDK fields and are available.
 * @note Existing NVS settings normally take precedence over compile-time values
 *       so credential-free GitHub firmware can reuse the network after OTA.
 */
static esp_err_t load_credentials(wifi_config_t * const configuration)
{
    nvs_handle_t handle = 0U;
    esp_err_t result = nvs_open("ota_net", NVS_READWRITE, &handle);
    char ssid[33] = {0};
    char password[65] = {0};
    if (result == ESP_OK)
    {
        size_t ssid_length = sizeof(ssid);
        size_t password_length = sizeof(password);
        result = nvs_get_str(handle, "ssid", ssid, &ssid_length);
        if (result == ESP_OK)
        {
            result = nvs_get_str(handle, "password", password, &password_length);
        }
        bool initial_configuration = (result == ESP_ERR_NVS_NOT_FOUND);
#if defined(CONFIG_MOUSE_OTA_WIFI_REPLACE_STORED)
        /* An explicit local bootstrap option is needed to replace an existing
         * network; an ordinary release must not silently overwrite saved data. */
        initial_configuration = true;
#endif
        if (initial_configuration)
        {
            const size_t initial_ssid_length = strlen(CONFIG_MOUSE_OTA_WIFI_SSID);
            const size_t initial_password_length = strlen(CONFIG_MOUSE_OTA_WIFI_PASSWORD);
            result = ESP_ERR_INVALID_STATE;
            if ((initial_ssid_length > 0U) && (initial_ssid_length < sizeof(ssid)) &&
                (initial_password_length < sizeof(password)))
            {
                (void)memcpy(ssid, CONFIG_MOUSE_OTA_WIFI_SSID, initial_ssid_length + 1U);
                (void)memcpy(password, CONFIG_MOUSE_OTA_WIFI_PASSWORD, initial_password_length + 1U);
                result = nvs_set_str(handle, "ssid", ssid);
                if (result == ESP_OK)
                {
                    result = nvs_set_str(handle, "password", password);
                }
                if (result == ESP_OK)
                {
                    /* Commit only after both strings were accepted. No password
                     * is logged or passed to GitHub requests. */
                    result = nvs_commit(handle);
                }
            }
        }
        if (result == ESP_OK)
        {
            const size_t stored_ssid_length = strlen(ssid);
            const size_t stored_password_length = strlen(password);
            if ((stored_ssid_length == 0U) || (stored_ssid_length > sizeof(configuration->sta.ssid)) ||
                (stored_password_length > sizeof(configuration->sta.password)))
            {
                result = ESP_ERR_INVALID_SIZE;
            }
            else
            {
                /* SDK arrays are fixed-width, not necessarily NUL-terminated when
                 * filled to capacity. Zero initialization supplies shorter tails. */
                (void)memcpy(configuration->sta.ssid, ssid, stored_ssid_length);
                (void)memcpy(configuration->sta.password, password, stored_password_length);
            }
        }
        nvs_close(handle);
    }
    return result;
}

esp_err_t ota_network_connect(void)
{
    wifi_config_t configuration = {0};
    esp_err_t result = load_credentials(&configuration);
    if (result != ESP_OK)
    {
        ESP_LOGW(TAG, "Set OTA Wi-Fi in local menuconfig before the first update");
    }
    if (result == ESP_OK)
    {
        result = esp_netif_init();
    }
    if (result == ESP_OK)
    {
        result = esp_event_loop_create_default();
        s_event_loop = (result == ESP_OK);
    }
    if (result == ESP_OK)
    {
        s_events = xEventGroupCreateStatic(&s_event_storage);
        s_station = esp_netif_create_default_wifi_sta();
        result = ((s_events != NULL) && (s_station != NULL)) ? ESP_OK : ESP_ERR_NO_MEM;
    }
    if (result == ESP_OK)
    {
        wifi_init_config_t initialization = WIFI_INIT_CONFIG_DEFAULT();
        result = esp_wifi_init(&initialization);
        s_wifi_initialized = (result == ESP_OK);
    }
    if (result == ESP_OK)
    {
        result = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
            wifi_callback, NULL, &s_wifi_handler);
        s_wifi_registered = (result == ESP_OK);
    }
    if (result == ESP_OK)
    {
        result = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
            wifi_callback, NULL, &s_ip_handler);
        s_ip_registered = (result == ESP_OK);
    }
    if (result == ESP_OK)
    {
        /* Credentials are managed by this component's NVS namespace, not the
         * Wi-Fi driver's own flash persistence. OTA must preserve those settings. */
        result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    }
    if (result == ESP_OK)
    {
        result = esp_wifi_set_mode(WIFI_MODE_STA);
    }
    if (result == ESP_OK)
    {
        result = esp_wifi_set_config(WIFI_IF_STA, &configuration);
    }
    if (result == ESP_OK)
    {
        s_retries = 0U;
        result = esp_wifi_start();
        s_wifi_started = (result == ESP_OK);
    }
    if (result == ESP_OK)
    {
        /* Waiting for IP rather than association alone ensures HTTPS has an
         * address; either failure signalling or the 30-second timeout can end it. */
        const EventBits_t bits = xEventGroupWaitBits(s_events,
            WIFI_CONNECTED_BIT | WIFI_FAILED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(30000U));
        result = ((bits & WIFI_CONNECTED_BIT) != 0U) ? ESP_OK : ESP_ERR_TIMEOUT;
    }
    if (result == ESP_OK)
    {
        const esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        result = esp_netif_sntp_init(&time_config);
        s_sntp_initialized = (result == ESP_OK);
    }
    if (result == ESP_OK)
    {
        /* Certificate validity checks remain enabled; never bypass TLS time. */
        result = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(30000U));
    }
    return result;
}

void ota_network_disconnect(void)
{
    /* Teardown is used after success and every partial failure. It releases the
     * maintenance transport but deliberately leaves NVS credentials untouched. */
    if (s_sntp_initialized)
    {
        esp_netif_sntp_deinit();
        s_sntp_initialized = false;
    }
    /* Remove handlers before stopping Wi-Fi so teardown never reconnects. */
    if (s_wifi_registered)
    {
        warn_result("Unregister Wi-Fi", esp_event_handler_instance_unregister(
            WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler));
        s_wifi_registered = false;
    }
    if (s_ip_registered)
    {
        warn_result("Unregister IP", esp_event_handler_instance_unregister(
            IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler));
        s_ip_registered = false;
    }
    if (s_wifi_started)
    {
        warn_result("Stop Wi-Fi", esp_wifi_stop());
        s_wifi_started = false;
    }
    if (s_wifi_initialized)
    {
        warn_result("Deinitialize Wi-Fi", esp_wifi_deinit());
        s_wifi_initialized = false;
    }
    if (s_station != NULL)
    {
        esp_netif_destroy_default_wifi(s_station);
        s_station = NULL;
    }
    if (s_event_loop)
    {
        warn_result("Delete maintenance event loop", esp_event_loop_delete_default());
        s_event_loop = false;
    }
    if (s_events != NULL)
    {
        vEventGroupDelete(s_events);
        s_events = NULL;
    }
}

/**
 * @brief Inspect Location response headers without automatically following them.
 * @return ESP_OK; the caller decides whether the recorded HTTPS location is safe
 *         to use after it has also checked the HTTP status code.
 */
static esp_err_t header_callback(esp_http_client_event_t *event)
{
    esp_err_t result = ESP_OK;
    if ((event != NULL) && (event->event_id == HTTP_EVENT_ON_HEADER) &&
        (event->header_key != NULL) && (event->header_value != NULL))
    {
        /* HTTP field names are case-insensitive; GitHub sends Location/location. */
        static const char expected[] = "location";
        bool location_header = (strlen(event->header_key) == (sizeof(expected) - 1U));
        for (size_t index = 0U; (index < (sizeof(expected) - 1U)) && location_header; ++index)
        {
            char character = event->header_key[index];
            if ((character >= 'A') && (character <= 'Z'))
            {
                character = (char)((uint8_t)character + UINT8_C(32));
            }
            location_header = (character == expected[index]);
        }
        if (location_header)
        {
            s_redirect_safe = (strncmp(event->header_value, "https://", 8U) == 0);
        }
    }
    return result;
}

esp_err_t ota_network_open_https(const char * const url,
                                 esp_http_client_handle_t * const client)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if ((url != NULL) && (client != NULL) && (strncmp(url, "https://", 8U) == 0))
    {
        const esp_http_client_config_t configuration = {
            .url = url,
            .timeout_ms = 15000,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = header_callback,
            .user_agent = "ESP32-Air-Mouse-OTA/1",
            .disable_auto_redirect = true,
            .max_redirection_count = 5,
            .keep_alive_enable = false,
            .buffer_size = 4096,
            .buffer_size_tx = 2048
        };
        *client = esp_http_client_init(&configuration);
        result = (*client != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
        bool opened = false;
        for (uint8_t attempt = 0U; (attempt < 6U) && (result == ESP_OK) && (!opened); ++attempt)
        {
            /* Automatic redirects are disabled so an HTTPS-to-HTTP downgrade is
             * rejected before a request can be sent to the redirected endpoint. */
            s_redirect_safe = false;
            result = esp_http_client_open(*client, 0);
            if (result == ESP_OK)
            {
                const int64_t response_length = esp_http_client_fetch_headers(*client);
                const int status = esp_http_client_get_status_code(*client);
                if (response_length < 0)
                {
                    result = ESP_ERR_INVALID_RESPONSE;
                }
                else if (status == 200)
                {
                    opened = true;
                }
                else if (((status == 301) || (status == 302) || (status == 303) ||
                          (status == 307) || (status == 308)) && s_redirect_safe)
                {
                    /* GitHub release assets move to CDN URLs. Accept those only
                     * through a checked HTTPS Location and verify the SDK's
                     * resulting transport before opening the next request. */
                    result = esp_http_client_set_redirection(*client);
                    if ((result == ESP_OK) &&
                        (esp_http_client_get_transport_type(*client) != HTTP_TRANSPORT_OVER_SSL))
                    {
                        result = ESP_ERR_INVALID_RESPONSE;
                    }
                    warn_result("Close redirect response", esp_http_client_close(*client));
                }
                else
                {
                    ESP_LOGW(TAG, "GitHub HTTP status %d or unsafe redirect", status);
                    result = ESP_ERR_INVALID_RESPONSE;
                }
            }
        }
        if ((result == ESP_OK) && (!opened))
        {
            result = ESP_ERR_TIMEOUT;
        }
        if ((result != ESP_OK) && (*client != NULL))
        {
            warn_result("Clean up HTTP connection", esp_http_client_cleanup(*client));
            *client = NULL;
        }
    }
    return result;
}

esp_err_t ota_network_fetch(const char * const url, char * const output,
                             size_t capacity, size_t * const length)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if ((url != NULL) && (output != NULL) && (length != NULL) && (capacity > 1U) &&
        (capacity <= (size_t)INT_MAX))
    {
        esp_http_client_handle_t client = NULL;
        result = ota_network_open_https(url, &client);
        size_t received = 0U;
        bool complete = false;
        const int64_t deadline = esp_timer_get_time() + INT64_C(30000000);
        while ((result == ESP_OK) && (!complete))
        {
            /* Reserve one byte for the final terminator. Reaching the reserved
             * boundary or deadline fails instead of truncating a manifest that
             * might otherwise appear valid to the policy parser. */
            if ((received >= (capacity - 1U)) || (esp_timer_get_time() >= deadline))
            {
                result = ESP_ERR_INVALID_SIZE;
            }
            else
            {
                const int count = esp_http_client_read(client, &output[received],
                    (int)(capacity - received - 1U));
                if (count < 0)
                {
                    result = ESP_FAIL;
                }
                else if (count == 0)
                {
                    /* Distinguish a complete body from a stalled/truncated read;
                     * partial metadata must never reach the manifest parser. */
                    complete = esp_http_client_is_complete_data_received(client);
                    result = complete ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
                }
                else
                {
                    received += (size_t)count;
                }
            }
        }
        if (client != NULL)
        {
            const esp_err_t cleanup = esp_http_client_cleanup(client);
            if (result == ESP_OK)
            {
                result = cleanup;
            }
        }
        if (result == ESP_OK)
        {
            output[received] = '\0';
            *length = received;
        }
    }
    return result;
}
