/* SPDX-License-Identifier: MIT */
/**
 * @file mouse_types.h
 * @brief Shared physical sensor units and logical HID mouse report fields.
 *
 * These types deliberately have no ESP-IDF dependency so the motion/input core
 * can be tested on a host. Sensor axes retain the MPU6050 coordinate system;
 * conversion to screen directions belongs to the motion component.
 */
#ifndef MOUSE_TYPES_H
#define MOUSE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/* HID Button usages 1..3 occupy bits 0..2; upper bits must remain zero.
 * Middle-button support is representable even though no physical key maps to it.
 */
#define MOUSE_BUTTON_LEFT   UINT8_C(0x01)
#define MOUSE_BUTTON_RIGHT  UINT8_C(0x02)
#define MOUSE_BUTTON_MIDDLE UINT8_C(0x04)
#define MOUSE_BUTTON_MASK   UINT8_C(0x07)

/** @brief One coherent six-axis sample after raw-register scale conversion. */
typedef struct
{
    float accel_x_g;  /**< Acceleration along sensor X, in multiples of gravity. */
    float accel_y_g;  /**< Acceleration along sensor Y, including gravity. */
    float accel_z_g;  /**< Acceleration along sensor Z, including gravity. */
    float gyro_x_dps; /**< Angular rate about sensor X, in degrees/second. */
    float gyro_y_dps; /**< Angular rate about sensor Y, in degrees/second. */
    float gyro_z_dps; /**< Angular rate about sensor Z, in degrees/second. */
} mouse_imu_sample_t;

/**
 * @brief Logical contents of one relative mouse input report.
 *
 * Buttons are absolute held/released state; X/Y/wheel are increments for this
 * report only. Serialize fields explicitly: compiler padding and struct layout
 * are not part of the BLE wire protocol. The BLE adapter handles the Report ID.
 */
typedef struct
{
    uint8_t buttons; /**< Bitwise combination of MOUSE_BUTTON_* held states. */
    int8_t x;       /**< Relative horizontal displacement in pointer counts. */
    int8_t y;       /**< Relative vertical displacement in pointer counts. */
    int8_t wheel;   /**< Relative wheel detents; positive means scroll up. */
} mouse_report_t;

#endif
