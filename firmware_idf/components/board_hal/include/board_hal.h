/* SPDX-License-Identifier: MIT */
/**
 * @file board_hal.h
 * @brief Hardware boundary for the ESP32-S3-WROOM-1-N16R8 mouse PCB.
 *
 * GPIO names follow the schematic. Button reads are raw; mouse_input owns
 * debouncing and gesture interpretation. Call this API from the main task.
 */
#ifndef BOARD_HAL_H
#define BOARD_HAL_H

#include <stdbool.h>
#include <stdint.h>
/* D-01: SDK headers intentionally use undefined config macros as zero. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "driver/i2c_master.h"
#include "esp_err.h"
#pragma GCC diagnostic pop

/* Single source of truth for schematic connections. Keep IMU_INT reserved even
 * though the current driver polls the sensor instead of using its interrupt. */
#define BOARD_I2C_SDA GPIO_NUM_5
#define BOARD_I2C_SCL GPIO_NUM_4
#define BOARD_IMU_INT GPIO_NUM_6
#define BOARD_BUTTON_LEFT GPIO_NUM_10
#define BOARD_BUTTON_RIGHT GPIO_NUM_11
#define BOARD_BUTTON_MODE GPIO_NUM_0
/* The LED anodes connect to 3.3 V: the MCU sinks current to turn them on. */
#define BOARD_LED_CONNECTION GPIO_NUM_12
#define BOARD_LED_SENSOR GPIO_NUM_13
#define BOARD_LED_SCROLL GPIO_NUM_14
#define BOARD_LED_ACTIVITY GPIO_NUM_21

/* Logical pressed-state masks, independent of the active-low GPIO polarity. */
#define BOARD_KEY_LEFT  UINT8_C(0x01)
#define BOARD_KEY_RIGHT UINT8_C(0x02)
#define BOARD_KEY_MODE  UINT8_C(0x04)

/**
 * @brief Validate N16R8 memory, configure GPIO and allocate the shared I2C bus.
 * @pre ESP-IDF startup has initialized flash and Octal PSRAM.
 * @return ESP_OK on success; ESP_ERR_INVALID_SIZE for an incompatible memory
 *         configuration; ESP_ERR_INVALID_STATE if already initialized; otherwise
 *         the first GPIO/I2C driver error.
 * @note Call once from the application task before other board operations.
 */
esp_err_t board_hal_init(void);

/**
 * @brief Borrow the shared I2C master bus created by board_hal_init().
 * @return Bus handle after successful initialization, or NULL before it.
 * @note The board owns this handle; clients must not delete the bus.
 */
i2c_master_bus_handle_t board_hal_i2c_bus(void);

/**
 * @brief Sample the three physical buttons without debouncing.
 * @return Bitwise OR of BOARD_KEY_* for pins currently at their pressed level.
 */
uint8_t board_hal_keys(void);

/**
 * @brief Set logical LED states while hiding the PCB's active-low wiring.
 * @param connected Connection-indicator state; true turns LED1 on.
 * @param sensor Sensor/calibration-indicator state; true turns LED2 on.
 * @param scroll Scroll-mode-indicator state; true turns LED3 on.
 * @param activity Activity-indicator state; true turns LED4 on.
 * @return ESP_OK or the first GPIO error. Earlier LED writes may already have
 *         taken effect when a later write fails; this is not an atomic update.
 */
esp_err_t board_hal_leds(bool connected, bool sensor, bool scroll, bool activity);

#endif
