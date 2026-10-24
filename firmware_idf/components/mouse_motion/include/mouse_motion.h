/* SPDX-License-Identifier: MIT */
#ifndef MOUSE_MOTION_H
#define MOUSE_MOTION_H

#include "mouse_types.h"

typedef struct
{
    float deadzone_degrees;
    float pixels_per_second_per_degree;
    float maximum_pixels_per_second;
    float wheel_steps_per_second_per_degree;
    bool invert_x;
    bool invert_y;
} mouse_motion_config_t;

typedef struct
{
    mouse_motion_config_t config;
    bool ready;
    uint16_t calibration_count;
    float sums[6];
    float squares[6];
    float gyro_bias[3];
    float roll_degrees;
    float pitch_degrees;
    float neutral_roll;
    float neutral_pitch;
    float residual_x;
    float residual_y;
    float residual_wheel;
    bool was_scrolling;
} mouse_motion_t;

typedef enum
{
    MOUSE_MOTION_INVALID = 0,
    MOUSE_MOTION_CALIBRATING,
    MOUSE_MOTION_CALIBRATION_RETRY,
    MOUSE_MOTION_READY
} mouse_motion_status_t;

bool mouse_motion_init(mouse_motion_t * const context,
                       const mouse_motion_config_t * const config);
mouse_motion_status_t mouse_motion_update(mouse_motion_t * const context,
    const mouse_imu_sample_t * const sample, float elapsed_seconds,
    bool scroll, mouse_report_t * const report);
void mouse_motion_recenter(mouse_motion_t * const context);
void mouse_motion_clear_residuals(mouse_motion_t * const context);

#endif
