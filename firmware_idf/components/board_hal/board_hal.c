/* SPDX-License-Identifier: MIT */
/**
 * @file board_hal.c
 * @brief Translate logical board operations into ESP-IDF GPIO/I2C calls.
 *
 * Board-specific polarity, pin assignments and memory checks stay here so the
 * sensor and mouse algorithms do not depend on PCB wiring details.
 */
#include "board_hal.h"
#include <stddef.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "driver/gpio.h"
#include "esp_psram.h"
#include "esp_flash.h"
#pragma GCC diagnostic pop

/* A single main-task owner shares this bus with the MPU driver. */
static i2c_master_bus_handle_t s_bus = NULL;

esp_err_t board_hal_leds(bool connected, bool sensor, bool scroll, bool activity)
{
    /* Stop at the first hardware error rather than replacing it with a later
     * successful write. Logical true maps to a low output on this board. */
    esp_err_t result = gpio_set_level(BOARD_LED_CONNECTION, connected ? 0U : 1U);
    if (result == ESP_OK)
    {
        result = gpio_set_level(BOARD_LED_SENSOR, sensor ? 0U : 1U);
    }
    if (result == ESP_OK)
    {
        result = gpio_set_level(BOARD_LED_SCROLL, scroll ? 0U : 1U);
    }
    if (result == ESP_OK)
    {
        result = gpio_set_level(BOARD_LED_ACTIVITY, activity ? 0U : 1U);
    }
    return result;
}

esp_err_t board_hal_init(void)
{
    esp_err_t result = ESP_ERR_INVALID_STATE;
    const gpio_config_t outputs = {
        .pin_bit_mask = (UINT64_C(1) << BOARD_LED_CONNECTION) |
                        (UINT64_C(1) << BOARD_LED_SENSOR) |
                        (UINT64_C(1) << BOARD_LED_SCROLL) |
                        (UINT64_C(1) << BOARD_LED_ACTIVITY),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    /* Internal pull-ups keep unused/disconnected switches in the released
     * state; the input component converts sampled edges into stable events. */
    const gpio_config_t inputs = {
        .pin_bit_mask = (UINT64_C(1) << BOARD_BUTTON_LEFT) |
                        (UINT64_C(1) << BOARD_BUTTON_RIGHT) |
                        (UINT64_C(1) << BOARD_BUTTON_MODE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    /* Internal pull-ups assist startup but do not replace correctly sized
     * external SDA/SCL pull-ups on the sensor module/PCB. */
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7U,
        .intr_priority = 0,
        .trans_queue_depth = 0U,
        .flags = {.enable_internal_pullup = 1U}
    };
    if (s_bus == NULL)
    {
        uint32_t flash_bytes = 0U;
        result = esp_flash_get_size(NULL, &flash_bytes);
        /* Fail before peripheral setup if this binary is running on a module
         * whose actual memory differs from the N16R8 partition/RAM preset. */
        if ((result == ESP_OK) &&
            ((flash_bytes != UINT32_C(0x1000000)) || (!esp_psram_is_initialized()) ||
             (esp_psram_get_size() != (size_t)UINT32_C(0x800000))))
        {
            result = ESP_ERR_INVALID_SIZE;
        }
        if (result == ESP_OK)
        {
            /* Preload active-low LED latches before enabling output drivers. */
            result = board_hal_leds(false, false, false, false);
        }
        if (result == ESP_OK)
        {
            result = gpio_config(&outputs);
        }
        if (result == ESP_OK)
        {
            result = gpio_config(&inputs);
        }
        if (result == ESP_OK)
        {
            result = i2c_new_master_bus(&bus_config, &s_bus);
        }
    }
    return result;
}

i2c_master_bus_handle_t board_hal_i2c_bus(void)
{
    return s_bus;
}

uint8_t board_hal_keys(void)
{
    /* Export logical pressed bits so callers never need to invert GPIO levels. */
    uint8_t keys = 0U;
    if (gpio_get_level(BOARD_BUTTON_LEFT) == 0)
    {
        keys |= BOARD_KEY_LEFT;
    }
    if (gpio_get_level(BOARD_BUTTON_RIGHT) == 0)
    {
        keys |= BOARD_KEY_RIGHT;
    }
    if (gpio_get_level(BOARD_BUTTON_MODE) == 0)
    {
        keys |= BOARD_KEY_MODE;
    }
    return keys;
}
