/* SPDX-License-Identifier: MIT */
/**
 * @file ota_update.c
 * @brief Install a newer GitHub Release into the inactive OTA slot and supervise
 *        first-boot confirmation before allowing that image to become permanent.
 *
 * All functions run in the main application task. Maintenance is a startup-only
 * path; no download or flash work is performed by the normal mouse loop.
 */
#include "ota_update.h"
#include "ota_network.h"
#include "ota_policy.h"
#include "board_hal.h"
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "sdkconfig.h"
#pragma GCC diagnostic pop

#define DOWNLOAD_DEADLINE_US INT64_C(180000000)
#define SELF_TEST_DEADLINE_US INT64_C(120000000)
#define HEALTHY_DURATION_US INT64_C(10000000)

static const char * const TAG = "ota_update";
/* Cached boot metadata and monotonic timestamps, owned by the main task.
 * Zero healthy-since means that no uninterrupted healthy interval is active. */
static bool s_pending = false;
static int64_t s_boot_started_us = 0;
static int64_t s_healthy_since_us = 0;
/* Reuse one bounded buffer across chunks instead of placing 4 KiB on each
 * nested download call's stack. Startup ownership prevents concurrent reuse. */
static uint8_t s_download_buffer[4096];

/** @brief Log cleanup/SDK failures without replacing the original error path. */
static void log_result(const char * const operation, esp_err_t result)
{
    if (result != ESP_OK)
    {
        ESP_LOGW(TAG, "%s: %s", operation, esp_err_to_name(result));
    }
}

void ota_update_boot_failed(void)
{
    if (!s_pending)
    {
        /* A fatal board/NVS initialization error can occur before startup has
         * cached boot state. Read it here so that early failures can roll back. */
        const esp_partition_t * const running = esp_ota_get_running_partition();
        esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
        if ((running != NULL) && (esp_ota_get_state_partition(running, &state) == ESP_OK))
        {
            s_pending = (state == ESP_OTA_IMG_PENDING_VERIFY);
        }
    }
    if (s_pending)
    {
        ESP_LOGE(TAG, "New image failed health check; rolling back");
        log_result("Rollback", esp_ota_mark_app_invalid_rollback_and_reboot());
    }
}

void ota_update_boot_health(bool healthy)
{
    if (s_pending)
    {
        const int64_t now = esp_timer_get_time();
        if (!healthy)
        {
            /* A momentary sensor fault invalidates the accumulated healthy time;
             * merely surviving ten seconds of wall time is not sufficient. */
            s_healthy_since_us = 0;
        }
        else if (s_healthy_since_us == 0)
        {
            s_healthy_since_us = now;
        }
        else if ((now - s_healthy_since_us) >= HEALTHY_DURATION_US)
        {
            const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
            if (result == ESP_OK)
            {
                s_pending = false;
                ESP_LOGI(TAG, "BLE, memory and MPU health passed; new image confirmed");
            }
            else
            {
                log_result("Confirm image", result);
            }
        }
        else
        {
            /* Continue observing application health. */
        }
        if (s_pending && ((now - s_boot_started_us) >= SELF_TEST_DEADLINE_US))
        {
            ota_update_boot_failed();
        }
    }
}

/**
 * @brief Accept only a bounded owner/repository identifier from configuration.
 * @return True for the allowed character set and exactly one interior slash.
 * @note This prevents configuration from inserting URL syntax; it is not a
 *       network lookup or a claim that the named GitHub repository exists.
 */
static bool repository_valid(void)
{
    const char * const repository = CONFIG_MOUSE_OTA_REPOSITORY;
    const size_t length = strlen(repository);
    uint8_t separators = 0U;
    bool valid = (length >= 3U) && (length <= 100U);
    for (size_t index = 0U; (index < length) && valid; ++index)
    {
        const char character = repository[index];
        if (character == '/')
        {
            ++separators;
            valid = (index > 0U) && (index < (length - 1U));
        }
        else
        {
            valid = ((character >= 'a') && (character <= 'z')) ||
                    ((character >= 'A') && (character <= 'Z')) ||
                    ((character >= '0') && (character <= '9')) ||
                    (character == '-') || (character == '_') || (character == '.');
        }
    }
    return valid && (separators == 1U);
}

/**
 * @brief Stream the opened HTTP body to an already-started inactive-slot writer.
 * @param client HTTP 200 response positioned at its body; owned by the caller.
 * @param writer OTA handle that remains owned by install_image().
 * @param manifest Checked expected byte count and digest from ota_policy.
 * @return ESP_OK only for a complete, exact-length body with a matching SHA-256.
 * @note This function neither ends the OTA writer nor changes boot selection.
 *       Its caller must abort the writer if any transport/hash/write check fails.
 */
static esp_err_t stream_image(esp_http_client_handle_t client, esp_ota_handle_t writer,
                              const ota_manifest_t * const manifest)
{
    esp_err_t result = ESP_OK;
    mbedtls_sha256_context digest;
    mbedtls_sha256_init(&digest);
    if (mbedtls_sha256_starts(&digest, 0) != 0)
    {
        result = ESP_FAIL;
    }
    uint32_t received = 0U;
    uint32_t next_progress = UINT32_C(262144);
    bool complete = false;
    const int64_t deadline = esp_timer_get_time() + DOWNLOAD_DEADLINE_US;
    while ((result == ESP_OK) && (!complete))
    {
        if (esp_timer_get_time() >= deadline)
        {
            result = ESP_ERR_TIMEOUT;
        }
        else
        {
            /* The SDK byte-stream API uses char; see D-09. */
            const int count = esp_http_client_read(client, (char *)s_download_buffer,
                                                   (int)sizeof(s_download_buffer));
            if (count < 0)
            {
                result = ESP_FAIL;
            }
            else if (count == 0)
            {
                complete = esp_http_client_is_complete_data_received(client);
                result = complete ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
            }
            else if ((uint32_t)count > (manifest->image_size - received))
            {
                /* Reject extra bytes before writing: the server cannot grow the
                 * downloaded image beyond the size approved by the manifest. */
                result = ESP_ERR_INVALID_SIZE;
            }
            else
            {
                result = esp_ota_write(writer, s_download_buffer, (size_t)count);
                if ((result == ESP_OK) &&
                    (mbedtls_sha256_update(&digest, s_download_buffer, (size_t)count) != 0))
                {
                    result = ESP_FAIL;
                }
                received += (uint32_t)count;
                if (received >= next_progress)
                {
                    ESP_LOGI(TAG, "Downloaded %lu / %lu bytes", (unsigned long)received,
                             (unsigned long)manifest->image_size);
                    next_progress += UINT32_C(262144);
                }
                vTaskDelay(pdMS_TO_TICKS(1U));
            }
        }
    }
    uint8_t actual_digest[32] = {0U};
    /* EOF alone is insufficient: require the declared byte count and hash before
     * permitting the caller to verify/finalize the image and select it for boot. */
    if ((result == ESP_OK) &&
        ((received != manifest->image_size) || (mbedtls_sha256_finish(&digest, actual_digest) != 0)))
    {
        result = ESP_ERR_INVALID_SIZE;
    }
    if ((result == ESP_OK) && (memcmp(actual_digest, manifest->sha256, sizeof(actual_digest)) != 0))
    {
        result = ESP_ERR_INVALID_CRC;
    }
    mbedtls_sha256_free(&digest);
    return result;
}

/**
 * @brief Download and validate one manifest-selected version into the next slot.
 * @param manifest Manifest that already passed board/size/version policy checks.
 * @return ESP_OK only after the inactive image is selected for the next boot.
 * @note Reboot is the caller's responsibility. Failures abort any open writer;
 *       the existing boot partition remains selected until all checks succeed.
 */
static esp_err_t install_image(const ota_manifest_t * const manifest)
{
    char url[256] = {0};
    const int count = snprintf(url, sizeof(url),
        "https://github.com/%s/releases/download/v%s/%s",
        CONFIG_MOUSE_OTA_REPOSITORY, manifest->version, OTA_ASSET_NAME);
    const esp_partition_t * const partition = esp_ota_get_next_update_partition(NULL);
    /* No manifest-controlled URL is accepted. Repository configuration, a strict
     * semantic version and the fixed board asset name define the download path. */
    esp_err_t result = ESP_ERR_INVALID_SIZE;
    if ((count > 0) && ((size_t)count < sizeof(url)) && (partition != NULL) &&
        (manifest->image_size <= partition->size))
    {
        esp_http_client_handle_t client = NULL;
        result = ota_network_open_https(url, &client);
        esp_ota_handle_t writer = 0U;
        bool writing = false;
        if (result == ESP_OK)
        {
            const int64_t content_length = esp_http_client_get_content_length(client);
            /* A positive Content-Length can be checked before flash is erased.
             * Streaming still verifies the exact total for unknown-length bodies. */
            if ((content_length > 0) && ((uint64_t)content_length != manifest->image_size))
            {
                result = ESP_ERR_INVALID_SIZE;
            }
        }
        if (result == ESP_OK)
        {
            result = esp_ota_begin(partition, manifest->image_size, &writer);
            writing = (result == ESP_OK);
        }
        if (result == ESP_OK)
        {
            result = stream_image(client, writer, manifest);
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
            esp_app_desc_t description = {0};
            /* A valid ESP image for another project/version must not be accepted
             * merely because the download's transport and digest were valid. */
            result = esp_ota_get_partition_description(partition, &description);
            if ((result == ESP_OK) &&
                ((strncmp(description.project_name, OTA_PROJECT_NAME, sizeof(description.project_name)) != 0) ||
                 (strncmp(description.version, manifest->version, sizeof(description.version)) != 0)))
            {
                result = ESP_ERR_INVALID_VERSION;
            }
        }
        if (result == ESP_OK)
        {
            /* End verifies the full ESP image/chip ID/checksum; it releases writer. */
            result = esp_ota_end(writer);
            writing = false;
        }
        if (result == ESP_OK)
        {
            /* This is the only operation that changes the next boot selection.
             * Bootloader rollback keeps the new image provisional until healthy. */
            result = esp_ota_set_boot_partition(partition);
        }
        if (writing)
        {
            log_result("Abort incomplete image", esp_ota_abort(writer));
        }
    }
    return result;
}

/**
 * @brief Fetch the latest stable-release manifest and install only a newer app.
 * @param[out] installed Set true only after installation and boot selection pass;
 *                       the caller initializes it false before this call.
 * @return ESP_OK also when the release is equal/older, because no update is due.
 */
static esp_err_t check_release(bool * const installed)
{
    char url[256] = {0};
    char text[OTA_MANIFEST_CAPACITY] = {0};
    size_t length = 0U;
    ota_manifest_t manifest = {0};
    const int count = snprintf(url, sizeof(url),
        "https://github.com/%s/releases/latest/download/%s",
        CONFIG_MOUSE_OTA_REPOSITORY, OTA_MANIFEST_NAME);
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (repository_valid() && (count > 0) && ((size_t)count < sizeof(url)))
    {
        result = ota_network_fetch(url, text, sizeof(text), &length);
        if ((result == ESP_OK) && (!ota_policy_parse(text, length, &manifest)))
        {
            result = ESP_ERR_INVALID_RESPONSE;
        }
        if (result == ESP_OK)
        {
            const esp_app_desc_t * const current = esp_app_get_description();
            if (ota_policy_newer(manifest.version, current->version))
            {
                ESP_LOGI(TAG, "Installing %s -> %s", current->version, manifest.version);
                result = install_image(&manifest);
                *installed = (result == ESP_OK);
            }
            else
            {
                ESP_LOGI(TAG, "Release %s is not newer than %s", manifest.version, current->version);
            }
        }
    }
    return result;
}

/**
 * @brief Recognize both mouse buttons continuously held during startup.
 * @return True after 150 samples separated by 20 ms retain the initial hold.
 * @note A release cancels immediately. This does not repurpose the BOOT strap
 *       and does not introduce a delay when neither update button is held.
 */
static bool maintenance_requested(void)
{
    const uint8_t mask = BOARD_KEY_LEFT | BOARD_KEY_RIGHT;
    bool held = ((board_hal_keys() & mask) == mask);
    if (held)
    {
        ESP_LOGI(TAG, "Keep both mouse buttons held for 3 seconds to enter OTA");
        for (uint16_t elapsed = 0U; (elapsed < 150U) && held; ++elapsed)
        {
            vTaskDelay(pdMS_TO_TICKS(20U));
            held = ((board_hal_keys() & mask) == mask);
        }
    }
    return held;
}

esp_err_t ota_update_startup(void)
{
    const esp_partition_t * const running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t result = ESP_OK;
    s_boot_started_us = esp_timer_get_time();
    if ((running != NULL) && (esp_ota_get_state_partition(running, &state) == ESP_OK))
    {
        s_pending = (state == ESP_OTA_IMG_PENDING_VERIFY);
    }
    if (s_pending)
    {
        /* An image must prove its own health before it may initiate another OTA;
         * otherwise repeated updates could bypass rollback confirmation. */
        ESP_LOGI(TAG, "Pending image: run health check before allowing another update");
    }
    else if (maintenance_requested())
    {
        log_result("OTA indicator", board_hal_leds(false, false, true, true));
        bool installed = false;
        result = ota_network_connect();
        if (result == ESP_OK)
        {
            result = check_release(&installed);
        }
        ota_network_disconnect();
        if (installed)
        {
            ESP_LOGI(TAG, "Image verified; rebooting into the new OTA slot");
            vTaskDelay(pdMS_TO_TICKS(250U));
            esp_restart();
        }
        log_result("OTA maintenance finished", result);
    }
    else
    {
        /* Normal BLE mouse startup does not enable Wi-Fi. */
    }
    return result;
}
