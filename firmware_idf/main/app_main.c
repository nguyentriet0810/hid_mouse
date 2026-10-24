/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include <stdint.h>
#include "ble_mouse.h"
#include "board_hal.h"
#include "mouse_input.h"
#include "mouse_motion.h"
#include "mpu6050.h"
#include "ota_update.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#pragma GCC diagnostic pop

#define LOOP_PERIOD_MS UINT32_C(10)
#define SENSOR_RETRY_US INT64_C(2000000)
#define MAX_READ_FAILURES UINT8_C(3)

static const char * const TAG = "mouse_app";

void app_main(void);

static const mouse_motion_config_t s_motion_config = {
    .deadzone_degrees = (float)CONFIG_MOUSE_DEADZONE_DEGREES,
    .pixels_per_second_per_degree = (float)CONFIG_MOUSE_GAIN,
    .maximum_pixels_per_second = (float)CONFIG_MOUSE_MAX_SPEED,
    .wheel_steps_per_second_per_degree = (float)CONFIG_MOUSE_WHEEL_GAIN_TENTHS / 10.0F,
#if defined(CONFIG_MOUSE_INVERT_X)
    .invert_x = true,
#else
    .invert_x = false,
#endif
#if defined(CONFIG_MOUSE_INVERT_Y)
    .invert_y = true
#else
    .invert_y = false
#endif
};

static esp_err_t initialize(void)
{
    esp_err_t result = board_hal_init();
    if (result == ESP_OK)
    {
        result = nvs_flash_init();
        if ((result == ESP_ERR_NVS_NO_FREE_PAGES) ||
            (result == ESP_ERR_NVS_NEW_VERSION_FOUND))
        {
            result = nvs_flash_erase();
            if (result == ESP_OK)
            {
                result = nvs_flash_init();
            }
        }
    }
    if (result == ESP_OK)
    {
        /* OTA failure leaves the current image selected; continue as a mouse. */
        const esp_err_t ota_result = ota_update_startup();
        if (ota_result != ESP_OK)
        {
            ESP_LOGW(TAG, "Continuing without OTA: %s", esp_err_to_name(ota_result));
        }
        result = ble_mouse_init();
    }
    return result;
}

static void run_mouse(void)
{
    mpu6050_t sensor = {0};
    mouse_motion_t motion = {0};
    mouse_input_t input = {0};
    TickType_t wake_tick = xTaskGetTickCount();
    int64_t retry_at_us = 0;
    int64_t previous_sample_us = 0;
    int64_t last_send_warning_us = 0;
    uint32_t generation = UINT32_MAX;
    uint8_t read_failures = 0U;
    uint8_t sent_buttons = 0U;
    bool sensor_online = false;
    bool led_error_reported = false;
    bool overflow_reported = false;
    bool configuration_valid = mouse_motion_init(&motion, &s_motion_config);

    if (configuration_valid)
    {
        for (;;)
        {
            int64_t now_us = esp_timer_get_time();
            const uint32_t now_ms = (uint32_t)((uint64_t)now_us / UINT64_C(1000));
            const ble_mouse_status_t link = ble_mouse_status();
            mouse_report_t report = {0};
            if (generation != link.generation)
            {
                generation = link.generation;
                mouse_input_reset(&input);
                sent_buttons = 0U;
                mouse_motion_clear_residuals(&motion);
            }
            mouse_input_update(&input, board_hal_keys(), now_ms, link.ready);
            if ((!sensor_online) && (now_us >= retry_at_us))
            {
                esp_err_t result = ESP_OK;
                if (sensor.device != NULL)
                {
                    result = mpu6050_deinit(&sensor);
                }
                if (result == ESP_OK)
                {
                    result = mpu6050_init(&sensor, board_hal_i2c_bus());
                }
                now_us = esp_timer_get_time();
                retry_at_us = now_us + SENSOR_RETRY_US;
                if (result == ESP_OK)
                {
                    sensor_online = true;
                    read_failures = 0U;
                    configuration_valid = mouse_motion_init(&motion, &s_motion_config);
                    previous_sample_us = now_us - INT64_C(10000);
                    wake_tick = xTaskGetTickCount();
                    ESP_LOGI(TAG, "MPU6050 found at 0x%02x; keep still for 2 seconds",
                             (unsigned int)sensor.address);
                }
                else
                {
                    ESP_LOGW(TAG, "MPU6050 unavailable: %s; retry in 2 seconds",
                             esp_err_to_name(result));
                }
            }
            if (sensor_online && configuration_valid)
            {
                mouse_imu_sample_t sample = {0};
                const esp_err_t result = mpu6050_read(&sensor, &sample);
                if (result == ESP_OK)
                {
                    read_failures = 0U;
                    const int64_t interval_us = now_us - previous_sample_us;
                    previous_sample_us = now_us;
                    const float interval = ((interval_us > 0) && (interval_us <= 50000)) ?
                        ((float)interval_us / 1000000.0F) : 0.01F;
                    const bool previously_ready = motion.ready;
                    const mouse_motion_status_t status = mouse_motion_update(&motion,
                        &sample, interval, input.scroll, &report);
                    if ((!previously_ready) && (status == MOUSE_MOTION_READY))
                    {
                        ESP_LOGI(TAG, "Calibration complete; neutral position captured");
                    }
                    else if (status == MOUSE_MOTION_CALIBRATION_RETRY)
                    {
                        ESP_LOGW(TAG, "Movement detected; keep still to retry calibration");
                    }
                    else
                    {
                        /* Calibration or normal running. */
                    }
                }
                else
                {
                    read_failures++;
                    previous_sample_us = now_us;
                    mouse_motion_clear_residuals(&motion);
                    if (read_failures >= MAX_READ_FAILURES)
                    {
                        sensor_online = false;
                        motion.ready = false;
                        retry_at_us = now_us + SENSOR_RETRY_US;
                        const esp_err_t cleanup = mpu6050_deinit(&sensor);
                        ESP_LOGW(TAG, "Sensor read failed: %s; cleanup: %s",
                            esp_err_to_name(result), esp_err_to_name(cleanup));
                    }
                }
            }
            if (input.recenter && motion.ready)
            {
                mouse_motion_recenter(&motion);
                report.x = 0;
                report.y = 0;
                report.wheel = 0;
                ESP_LOGI(TAG, "Neutral position recentered");
            }
            /* Suppress pointer movement while waiting for BOOT hold detection. */
            if (((input.stable_keys & MOUSE_INPUT_MODE_MASK) != 0U) && (!input.scroll))
            {
                report.x = 0;
                report.y = 0;
                mouse_motion_clear_residuals(&motion);
            }
            if (!link.ready)
            {
                mouse_motion_clear_residuals(&motion);
            }
            else
            {
                report.buttons = sent_buttons;
                const bool button_event = mouse_input_peek(&input, &report.buttons);
                const bool movement = (report.x != 0) || (report.y != 0) || (report.wheel != 0);
                if (button_event || movement)
                {
                    const esp_err_t result = ble_mouse_send(&report);
                    if (result == ESP_OK)
                    {
                        sent_buttons = report.buttons;
                        if (button_event)
                        {
                            mouse_input_ack(&input);
                        }
                    }
                    else if ((now_us - last_send_warning_us) >= INT64_C(2000000))
                    {
                        ESP_LOGW(TAG, "HID report pending: %s", esp_err_to_name(result));
                        last_send_warning_us = now_us;
                    }
                    else
                    {
                        /* Button edge stays queued for the next report tick. */
                    }
                }
            }
            if (input.overflow && (!overflow_reported))
            {
                ESP_LOGE(TAG, "Button queue full: forcing release; release physical buttons");
            }
            overflow_reported = input.overflow;
            ota_update_boot_health(sensor_online && motion.ready && configuration_valid &&
                                   (read_failures == 0U));
            const bool sensor_led = sensor_online &&
                (motion.ready || (((now_ms / UINT32_C(250)) % 2U) != 0U));
            const bool link_led = link.ready || ((!link.connected) &&
                (((now_ms / UINT32_C(500)) % 2U) != 0U));
            const bool activity = ((input.stable_keys & UINT8_C(0x03)) != 0U) ||
                (report.x != 0) || (report.y != 0) || (report.wheel != 0);
            const esp_err_t led_result = board_hal_leds(link_led, sensor_led,
                                                       input.scroll, activity);
            if ((led_result != ESP_OK) && (!led_error_reported))
            {
                ESP_LOGE(TAG, "LED update: %s", esp_err_to_name(led_result));
                led_error_reported = true;
            }
            (void)xTaskDelayUntil(&wake_tick, pdMS_TO_TICKS(LOOP_PERIOD_MS));
        }
    }
    else
    {
        ESP_LOGE(TAG, "Invalid motion configuration");
    }
}

void app_main(void)
{
    const esp_err_t result = initialize();
    if (result == ESP_OK)
    {
        ESP_LOGI(TAG, "ESP-IDF 5.3.1 BLE HID mouse starting");
        run_mouse();
    }
    else
    {
        ESP_LOGE(TAG, "Startup failed: %s", esp_err_to_name(result));
        ota_update_boot_failed();
    }
}
