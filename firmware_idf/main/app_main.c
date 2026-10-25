/* SPDX-License-Identifier: MIT */
/**
 * @file app_main.c
 * @brief Single-task coordinator for the N16R8 BLE air mouse and OTA boot checks.
 *
 * The application task owns GPIO sampling, MPU access, filter state and button
 * ordering. BLE callbacks communicate through status snapshots; they do not touch
 * these task-local contexts. Startup maintenance completes before BLE is enabled.
 */
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
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#pragma GCC diagnostic pop

/** Nominal 100 Hz sampling/report opportunity; I2C calls retain their own timeouts. */
#define LOOP_PERIOD_MS UINT32_C(10)
/** Keep a missing sensor from monopolizing the bus or flooding the log. */
#define SENSOR_RETRY_US INT64_C(2000000)
#define MAX_READ_FAILURES UINT8_C(3)

static const char * const TAG = "mouse_app";

void app_main(void);

/** Return a stable label for the boot reason without allocating memory. */
static const char *reset_reason_name(esp_reset_reason_t reason)
{
    const char *name = "unrecognized";
    switch (reason)
    {
        case ESP_RST_UNKNOWN:
            name = "unknown";
            break;
        case ESP_RST_POWERON:
            name = "poweron";
            break;
        case ESP_RST_EXT:
            name = "ext";
            break;
        case ESP_RST_SW:
            name = "sw";
            break;
        case ESP_RST_PANIC:
            name = "panic";
            break;
        case ESP_RST_INT_WDT:
            name = "int_wdt";
            break;
        case ESP_RST_TASK_WDT:
            name = "task_wdt";
            break;
        case ESP_RST_WDT:
            name = "wdt";
            break;
        case ESP_RST_DEEPSLEEP:
            name = "deepsleep";
            break;
        case ESP_RST_BROWNOUT:
            name = "brownout";
            break;
        case ESP_RST_SDIO:
            name = "sdio";
            break;
        case ESP_RST_USB:
            name = "usb";
            break;
        case ESP_RST_JTAG:
            name = "jtag";
            break;
        case ESP_RST_EFUSE:
            name = "efuse";
            break;
        case ESP_RST_PWR_GLITCH:
            name = "pwr_glitch";
            break;
        case ESP_RST_CPU_LOCKUP:
            name = "cpu_lockup";
            break;
        default:
            break;
    }
    return name;
}

/** Compile-time menuconfig values translated into physical motion units. */
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

/**
 * @brief Validate board hardware, initialize persistent storage, then start BLE.
 * @return ESP_OK when normal mouse operation may start; otherwise a fatal startup error.
 *
 * Only the two documented recoverable NVS errors erase/recreate storage. This
 * recovery also clears saved bonds/credentials. An OTA maintenance failure is
 * logged but does not make the currently selected mouse firmware unusable.
 */
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

/**
 * @brief Own and schedule the sensor/input/report pipeline until reset.
 *
 * Healthy normal operation stays in this loop. A missing MPU suppresses motion
 * while buttons and BLE continue; recovery reinitializes and recalibrates it.
 * Each tick offers at most one report to the SDK. Button FIFO entries are removed
 * only on SDK acceptance, whereas stale relative movement is not replayed later.
 */
static void run_mouse(void)
{
    mpu6050_t sensor = {0};
    mouse_motion_t motion = {0};
    mouse_input_t input = {0};
    TickType_t wake_tick = xTaskGetTickCount();
    /* Monotonic microsecond deadlines are separate from wrapping input milliseconds. */
    int64_t retry_at_us = 0;
    int64_t previous_sample_us = 0;
    int64_t last_send_warning_us = 0;
#if defined(CONFIG_MOUSE_BLE_DIAGNOSTICS)
    /* Task-owned counters wrap as unsigned values; they never gate operation. */
    int64_t diagnostic_at_us = 0;
    uint32_t accepted_reports = 0U;
    uint32_t rejected_reports = 0U;
    uint32_t movement_ticks = 0U;
#endif
    /* A BLE session boundary must invalidate all pending clicks and held state. */
    uint32_t generation = UINT32_MAX;
    uint8_t read_failures = 0U;
    uint8_t sent_buttons = 0U; /* Last button state accepted by the Bluetooth SDK. */
    bool sensor_online = false;
    bool led_error_reported = false;
    bool overflow_reported = false;
    bool configuration_valid = mouse_motion_init(&motion, &s_motion_config);

    if (configuration_valid)
    {
        for (;;)
        {
            int64_t now_us = esp_timer_get_time();
            /* Intentional uint32 wrap matches mouse_input's elapsed-time arithmetic. */
            const uint32_t now_ms = (uint32_t)((uint64_t)now_us / UINT64_C(1000));
            const ble_mouse_status_t link = ble_mouse_status();
            mouse_report_t report = {0};
            /* 1. Synchronize link/input state before considering this tick's motion. */
            if (generation != link.generation)
            {
                generation = link.generation;
                mouse_input_reset(&input);
                sent_buttons = 0U;
                mouse_motion_clear_residuals(&motion);
            }
            mouse_input_update(&input, board_hal_keys(), now_ms, link.ready);
            /* 2. Retry sensor setup only on its deadline; retain button service. */
            if ((!sensor_online) && (now_us >= retry_at_us))
            {
                esp_err_t result = ESP_OK;
                /* Retry cleanup too if an earlier device-removal call failed. */
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
                    /* Setup includes delays: begin with one nominal sample interval,
                     * and reset the scheduler baseline instead of catching up in a burst. */
                    previous_sample_us = now_us - INT64_C(10000);
                    wake_tick = xTaskGetTickCount();
                    ESP_LOGI(TAG, "IMU %s found at 0x%02x; keep still for 2 seconds",
                             mpu6050_model_name(&sensor), (unsigned int)sensor.address);
                }
                else
                {
                    ESP_LOGW(TAG, "IMU unavailable: %s; see mpu6050 identity/transport log; retry in 2 seconds",
                             esp_err_to_name(result));
                }
            }
            /* 3. Convert one coherent MPU reading into calibrated relative movement. */
            if (sensor_online && configuration_valid)
            {
                mouse_imu_sample_t sample = {0};
                const esp_err_t result = mpu6050_read(&sensor, &sample);
                if (result == ESP_OK)
                {
                    read_failures = 0U;
                    const int64_t interval_us = now_us - previous_sample_us;
                    previous_sample_us = now_us;
                    /* A long stall must not become an excessive integrated gyro step. */
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
                    /* Escalate repeated transport failures to a fresh calibration;
                     * isolated failures simply leave this tick's movement at zero. */
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
            /* 4. Apply the mode gesture after updating attitude, using its latest value. */
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
            /* 5. Send only current-session data. Relative displacement accumulated
             * without a usable host must not jump the pointer after reconnection. */
#if defined(CONFIG_MOUSE_BLE_DIAGNOSTICS)
            if ((report.x != 0) || (report.y != 0) || (report.wheel != 0))
            {
                movement_ticks++;
            }
#endif
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
#if defined(CONFIG_MOUSE_BLE_DIAGNOSTICS)
                        accepted_reports++;
#endif
                        sent_buttons = report.buttons;
                        /* SDK acceptance is the queue checkpoint, not a delay or retry count. */
                        if (button_event)
                        {
                            mouse_input_ack(&input);
                        }
                    }
                    else
                    {
#if defined(CONFIG_MOUSE_BLE_DIAGNOSTICS)
                        rejected_reports++;
#endif
                        if ((now_us - last_send_warning_us) >= INT64_C(2000000))
                        {
                            ESP_LOGW(TAG, "HID report pending: %s", esp_err_to_name(result));
                            last_send_warning_us = now_us;
                        }
                        /* Button edge stays queued for the next report tick. */
                    }
                }
            }
            if (input.overflow && (!overflow_reported))
            {
                ESP_LOGE(TAG, "Button queue full: forcing release; release physical buttons");
            }
            overflow_reported = input.overflow;
            /* 6. Feed rollback health without requiring a currently paired host. */
            ota_update_boot_health(sensor_online && motion.ready && configuration_valid &&
                                   (read_failures == 0U));
            /* LED states expose calibration, connectivity, scroll mode and activity. */
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
#if defined(CONFIG_MOUSE_BLE_DIAGNOSTICS)
            if (now_us >= diagnostic_at_us)
            {
                /* A periodic line proves that the owning task is still running.
                 * accepted counts SDK enqueue success, not delivery to the host. */
                ESP_LOGI(TAG, "HID health: connected=%u encrypted=%u subscribed=%u suspended=%u congested=%u ready=%u",
                    (unsigned int)link.connected, (unsigned int)link.encrypted,
                    (unsigned int)link.subscribed, (unsigned int)link.suspended,
                    (unsigned int)link.congested, (unsigned int)link.ready);
                ESP_LOGI(TAG, "Runtime: imu=%u calibrated=%u keys=0x%02x movement=%lu accepted=%lu rejected=%lu internal_heap=%lu min_internal_heap=%lu reset=%s",
                    (unsigned int)sensor_online, (unsigned int)motion.ready,
                    (unsigned int)input.stable_keys, (unsigned long)movement_ticks,
                    (unsigned long)accepted_reports, (unsigned long)rejected_reports,
                    (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                    (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                    reset_reason_name(esp_reset_reason()));
                diagnostic_at_us = now_us + INT64_C(10000000);
            }
#endif
            /* Delay against the previous wake time to avoid adding work duration
             * to every nominal 10 ms period. A missed deadline is not an API error. */
            (void)xTaskDelayUntil(&wake_tick, pdMS_TO_TICKS(LOOP_PERIOD_MS));
        }
    }
    else
    {
        ESP_LOGE(TAG, "Invalid motion configuration");
    }
}

/**
 * @brief ESP-IDF entry point; start the application or report a fatal startup error.
 * @note A pending OTA image requests rollback on fatal initialization failure.
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Firmware %s; boot reset=%s (%u)",
        esp_app_get_description()->version, reset_reason_name(esp_reset_reason()),
        (unsigned int)esp_reset_reason());
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
