/* SPDX-License-Identifier: MIT */
#include "mouse_motion.h"
#include <math.h>
#include <stddef.h>

#define CALIBRATION_SAMPLES UINT16_C(200)
#define RADIANS_TO_DEGREES 57.2957795F

static float wrap_degrees(float angle)
{
    float wrapped = angle;
    if (wrapped > 180.0F)
    {
        wrapped -= 360.0F;
    }
    else if (wrapped < -180.0F)
    {
        wrapped += 360.0F;
    }
    else
    {
        /* Already in range; inputs are bounded to [-540, 540]. */
    }
    return wrapped;
}

static float clamp(float value, float limit)
{
    float bounded = value;
    if (bounded > limit)
    {
        bounded = limit;
    }
    else if (bounded < -limit)
    {
        bounded = -limit;
    }
    else
    {
        /* In range. */
    }
    return bounded;
}

static float remove_deadzone(float angle, float deadzone)
{
    float value = 0.0F;
    if (angle > deadzone)
    {
        value = angle - deadzone;
    }
    else if (angle < -deadzone)
    {
        value = angle + deadzone;
    }
    else
    {
        /* No velocity in the neutral region. */
    }
    return value;
}

static int8_t quantize(float displacement, float * const residual)
{
    const float bounded = clamp(displacement + *residual, 127.0F);
    /* Explicit conversion is safe only after finite validation and saturation. */
    const int8_t integral = (int8_t)bounded;
    *residual = bounded - (float)integral;
    return integral;
}

void mouse_motion_clear_residuals(mouse_motion_t * const context)
{
    if (context != NULL)
    {
        context->residual_x = 0.0F;
        context->residual_y = 0.0F;
        context->residual_wheel = 0.0F;
    }
}

void mouse_motion_recenter(mouse_motion_t * const context)
{
    if (context != NULL)
    {
        if (context->ready)
        {
            context->neutral_roll = context->roll_degrees;
            context->neutral_pitch = context->pitch_degrees;
            mouse_motion_clear_residuals(context);
        }
    }
}

bool mouse_motion_init(mouse_motion_t * const context,
                       const mouse_motion_config_t * const config)
{
    bool valid = false;
    if ((context != NULL) && (config != NULL))
    {
        valid = (isfinite(config->deadzone_degrees) != 0) &&
                (isfinite(config->pixels_per_second_per_degree) != 0) &&
                (isfinite(config->maximum_pixels_per_second) != 0) &&
                (isfinite(config->wheel_steps_per_second_per_degree) != 0) &&
                (config->deadzone_degrees >= 0.0F) &&
                (config->deadzone_degrees <= 45.0F) &&
                (config->pixels_per_second_per_degree > 0.0F) &&
                (config->pixels_per_second_per_degree <= 200.0F) &&
                (config->maximum_pixels_per_second > 0.0F) &&
                (config->maximum_pixels_per_second <= 10000.0F) &&
                (config->wheel_steps_per_second_per_degree > 0.0F) &&
                (config->wheel_steps_per_second_per_degree <= 10.0F);
        if (valid)
        {
            const mouse_motion_t initial = {.config = *config};
            *context = initial;
        }
    }
    return valid;
}

static mouse_motion_status_t calibrate(mouse_motion_t * const context,
                                       const float values[6])
{
    mouse_motion_status_t status = MOUSE_MOTION_CALIBRATING;
    for (uint8_t index = 0U; index < 6U; ++index)
    {
        context->sums[index] += values[index];
        context->squares[index] += values[index] * values[index];
    }
    context->calibration_count++;
    if (context->calibration_count == CALIBRATION_SAMPLES)
    {
        float mean[6] = {0.0F};
        bool stationary = true;
        for (uint8_t index = 0U; index < 6U; ++index)
        {
            mean[index] = context->sums[index] / (float)CALIBRATION_SAMPLES;
            const float variance = (context->squares[index] /
                (float)CALIBRATION_SAMPLES) - (mean[index] * mean[index]);
            const float maximum_variance = (index < 3U) ? 0.0025F : 1.0F;
            if (variance > maximum_variance)
            {
                stationary = false;
            }
        }
        const float norm_squared = (mean[0] * mean[0]) + (mean[1] * mean[1]) +
                                   (mean[2] * mean[2]);
        if ((norm_squared < 0.81F) || (norm_squared > 1.21F))
        {
            stationary = false;
        }
        for (uint8_t index = 3U; index < 6U; ++index)
        {
            if (fabsf(mean[index]) > 5.0F)
            {
                stationary = false;
            }
        }
        if (stationary)
        {
            for (uint8_t index = 0U; index < 3U; ++index)
            {
                context->gyro_bias[index] = mean[index + 3U];
            }
            context->roll_degrees = atan2f(mean[1], mean[2]) * RADIANS_TO_DEGREES;
            context->pitch_degrees = atan2f(-mean[0],
                sqrtf((mean[1] * mean[1]) + (mean[2] * mean[2]))) * RADIANS_TO_DEGREES;
            context->ready = true;
            mouse_motion_recenter(context);
            status = MOUSE_MOTION_READY;
        }
        else
        {
            for (uint8_t index = 0U; index < 6U; ++index)
            {
                context->sums[index] = 0.0F;
                context->squares[index] = 0.0F;
            }
            context->calibration_count = 0U;
            status = MOUSE_MOTION_CALIBRATION_RETRY;
        }
    }
    return status;
}

mouse_motion_status_t mouse_motion_update(mouse_motion_t * const context,
    const mouse_imu_sample_t * const sample, float elapsed_seconds,
    bool scroll, mouse_report_t * const report)
{
    mouse_motion_status_t status = MOUSE_MOTION_INVALID;
    if ((context != NULL) && (sample != NULL) && (report != NULL))
    {
        report->x = 0;
        report->y = 0;
        report->wheel = 0;
        const float values[6] = {sample->accel_x_g, sample->accel_y_g,
            sample->accel_z_g, sample->gyro_x_dps, sample->gyro_y_dps,
            sample->gyro_z_dps};
        bool valid = (isfinite(elapsed_seconds) != 0) && (elapsed_seconds > 0.0F) &&
                     (elapsed_seconds <= 0.05F);
        for (uint8_t index = 0U; index < 6U; ++index)
        {
            const float limit = (index < 3U) ? 2.1F : 501.0F;
            if ((isfinite(values[index]) == 0) || (fabsf(values[index]) > limit))
            {
                valid = false;
            }
        }
        if (valid)
        {
            if (!context->ready)
            {
                status = calibrate(context, values);
            }
            else
            {
                const float norm_squared = (values[0] * values[0]) +
                    (values[1] * values[1]) + (values[2] * values[2]);
                float roll = wrap_degrees(context->roll_degrees +
                    ((values[3] - context->gyro_bias[0]) * elapsed_seconds));
                float pitch = wrap_degrees(context->pitch_degrees +
                    ((values[4] - context->gyro_bias[1]) * elapsed_seconds));
                if ((norm_squared > 0.64F) && (norm_squared < 1.44F))
                {
                    const float accel_roll = atan2f(values[1], values[2]) *
                                             RADIANS_TO_DEGREES;
                    const float accel_pitch = atan2f(-values[0],
                        sqrtf((values[1] * values[1]) + (values[2] * values[2]))) *
                        RADIANS_TO_DEGREES;
                    const float correction = elapsed_seconds / (0.5F + elapsed_seconds);
                    roll = wrap_degrees(roll + correction * wrap_degrees(accel_roll - roll));
                    pitch = wrap_degrees(pitch + correction * wrap_degrees(accel_pitch - pitch));
                }
                context->roll_degrees = roll;
                context->pitch_degrees = pitch;
                const float horizontal = remove_deadzone(wrap_degrees(pitch -
                    context->neutral_pitch), context->config.deadzone_degrees);
                const float vertical = remove_deadzone(wrap_degrees(roll -
                    context->neutral_roll), context->config.deadzone_degrees);
                if (context->was_scrolling != scroll)
                {
                    mouse_motion_clear_residuals(context);
                }
                context->was_scrolling = scroll;
                if (scroll)
                {
                    report->wheel = quantize(-vertical *
                        context->config.wheel_steps_per_second_per_degree *
                        elapsed_seconds, &context->residual_wheel);
                }
                else
                {
                    const float sign_x = context->config.invert_x ? 1.0F : -1.0F;
                    const float sign_y = context->config.invert_y ? -1.0F : 1.0F;
                    report->x = quantize(clamp(sign_x * horizontal *
                        context->config.pixels_per_second_per_degree,
                        context->config.maximum_pixels_per_second) * elapsed_seconds,
                        &context->residual_x);
                    report->y = quantize(clamp(sign_y * vertical *
                        context->config.pixels_per_second_per_degree,
                        context->config.maximum_pixels_per_second) * elapsed_seconds,
                        &context->residual_y);
                }
                status = MOUSE_MOTION_READY;
            }
        }
    }
    return status;
}
