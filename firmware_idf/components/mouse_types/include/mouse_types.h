/* SPDX-License-Identifier: MIT */
#ifndef MOUSE_TYPES_H
#define MOUSE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define MOUSE_BUTTON_LEFT   UINT8_C(0x01)
#define MOUSE_BUTTON_RIGHT  UINT8_C(0x02)
#define MOUSE_BUTTON_MIDDLE UINT8_C(0x04)
#define MOUSE_BUTTON_MASK   UINT8_C(0x07)

/* Physical units at the sensor boundary: g and degrees/second. */
typedef struct
{
    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    float gyro_x_dps;
    float gyro_y_dps;
    float gyro_z_dps;
} mouse_imu_sample_t;

/* Serialize fields explicitly; never transmit this structure's memory layout. */
typedef struct
{
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
} mouse_report_t;

#endif
