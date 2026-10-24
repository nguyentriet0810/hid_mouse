/* SPDX-License-Identifier: MIT */
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

/* Single source of truth for schematic connections. */
#define BOARD_I2C_SDA GPIO_NUM_5
#define BOARD_I2C_SCL GPIO_NUM_4
#define BOARD_IMU_INT GPIO_NUM_6
#define BOARD_BUTTON_LEFT GPIO_NUM_10
#define BOARD_BUTTON_RIGHT GPIO_NUM_11
#define BOARD_BUTTON_MODE GPIO_NUM_0
#define BOARD_LED_CONNECTION GPIO_NUM_12
#define BOARD_LED_SENSOR GPIO_NUM_13
#define BOARD_LED_SCROLL GPIO_NUM_14
#define BOARD_LED_ACTIVITY GPIO_NUM_21

#define BOARD_KEY_LEFT  UINT8_C(0x01)
#define BOARD_KEY_RIGHT UINT8_C(0x02)
#define BOARD_KEY_MODE  UINT8_C(0x04)

/* Call once from the application task before accessing the board. */
esp_err_t board_hal_init(void);
i2c_master_bus_handle_t board_hal_i2c_bus(void);
uint8_t board_hal_keys(void);
esp_err_t board_hal_leds(bool connected, bool sensor, bool scroll, bool activity);

#endif
