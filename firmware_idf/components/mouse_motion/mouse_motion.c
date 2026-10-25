/* SPDX-License-Identifier: MIT */
/**
 * @file mouse_motion.c
 * @brief Convert sensor attitude relative to a captured neutral pose into mouse velocity.
 *
 * Calibration and filtering stay independent of the RTOS, sensor driver and BLE.
 * A complementary filter uses gyro response for short-term motion and gravity
 * for long-term drift correction; this two-axis model targets moderate hand tilt,
 * not unrestricted three-dimensional orientation tracking.
 */
#include "mouse_motion.h"
#include <math.h>
#include <stddef.h>

/* At the nominal 100 Hz sample rate, two seconds of stillness establish bias
 * and neutral attitude. Counting accepted samples avoids assuming wall time.
 */
#define CALIBRATION_SAMPLES UINT16_C(200)
#define RADIANS_TO_DEGREES 57.2957795F

/* Keep angle differences continuous across +/-180 degrees. A single turn of
 * correction is sufficient because all callers supply already bounded angles.
 */
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

/* Symmetric saturation bounds speed and makes later signed narrowing explicit. */
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

/* Subtract the deadzone width rather than jumping immediately to the raw angle:
 * pointer speed then grows continuously from zero at either neutral boundary.
 */
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

/* Preserve sub-count motion so slow tilts do not disappear through truncation.
 * Saturation intentionally discards excess whole counts, preventing a backlog
 * that would keep moving the pointer after a fast gesture has stopped.
 */
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
            /* Recenter changes the zero-velocity pose, not the gyro bias. Clearing
             * residuals also prevents a small pre-recenter step on the next report.
             */
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
        /* Check both finiteness and scale limits before configuration is copied;
         * downstream conversion can then rely on bounded floating-point values.
         */
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

/* Accept a complete stationary window before trusting either bias or pose.
 * Sums and squared sums avoid retaining a sample history in RAM.
 */
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
            /* Acceleration variance is in g^2; gyro variance is in (degrees/s)^2.
             * Different physical units require different stillness thresholds.
             */
            const float maximum_variance = (index < 3U) ? 0.0025F : 1.0F;
            if (variance > maximum_variance)
            {
                stationary = false;
            }
        }
        /* A stationary sensor must see approximately 1 g. This also rejects an
         * all-zero read stream that would otherwise appear perfectly motionless.
         */
        const float norm_squared = (mean[0] * mean[0]) + (mean[1] * mean[1]) +
                                   (mean[2] * mean[2]);
        if ((norm_squared < 0.81F) || (norm_squared > 1.21F))
        {
            stationary = false;
        }
        /* Low variance alone cannot distinguish stillness from constant rotation;
         * reject a large mean rate instead of learning that rotation as gyro bias.
         */
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
            /* Gravity supplies the initial attitude. Capturing it as neutral
             * permits calibration in the user's comfortable holding position.
             */
            context->roll_degrees = atan2f(mean[1], mean[2]) * RADIANS_TO_DEGREES;
            context->pitch_degrees = atan2f(-mean[0],
                sqrtf((mean[1] * mean[1]) + (mean[2] * mean[2]))) * RADIANS_TO_DEGREES;
            context->ready = true;
            mouse_motion_recenter(context);
            status = MOUSE_MOTION_READY;
        }
        else
        {
            /* Discard the entire moving window: mixing it into the next window
             * would bias the first usable calibration after the hand settles.
             */
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
        /* The input component owns button state. Only motion fields are cleared
         * so invalid/calibrating samples cannot emit stale movement or erase clicks.
         */
        report->x = 0;
        report->y = 0;
        report->wheel = 0;
        const float values[6] = {sample->accel_x_g, sample->accel_y_g,
            sample->accel_z_g, sample->gyro_x_dps, sample->gyro_y_dps,
            sample->gyro_z_dps};
        /* Long gaps are rejected instead of integrating a stale gyro rate over
         * them. The slight physical-range margins allow scale/rounding tolerance.
         */
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
                /* Gyro prediction responds promptly to movement; subtract the
                 * learned stationary bias before integrating the measured dt.
                 */
                float roll = wrap_degrees(context->roll_degrees +
                    ((values[3] - context->gyro_bias[0]) * elapsed_seconds));
                float pitch = wrap_degrees(context->pitch_degrees +
                    ((values[4] - context->gyro_bias[1]) * elapsed_seconds));
                /* Strong linear acceleration is not a reliable gravity direction.
                 * Apply gravity correction only while magnitude lies near 1 g.
                 */
                if ((norm_squared > 0.64F) && (norm_squared < 1.44F))
                {
                    const float accel_roll = atan2f(values[1], values[2]) *
                                             RADIANS_TO_DEGREES;
                    const float accel_pitch = atan2f(-values[0],
                        sqrtf((values[1] * values[1]) + (values[2] * values[2]))) *
                        RADIANS_TO_DEGREES;
                    /* A 0.5 s time constant removes long-term gyro drift while
                     * preserving short-term response. Weighting uses actual dt.
                     */
                    const float correction = elapsed_seconds / (0.5F + elapsed_seconds);
                    roll = wrap_degrees(roll + correction * wrap_degrees(accel_roll - roll));
                    pitch = wrap_degrees(pitch + correction * wrap_degrees(accel_pitch - pitch));
                }
                context->roll_degrees = roll;
                context->pitch_degrees = pitch;
                /* Relative tilt sets velocity, not an absolute screen coordinate:
                 * returning to neutral stops motion without moving the cursor back.
                 */
                const float horizontal = remove_deadzone(wrap_degrees(pitch -
                    context->neutral_pitch), context->config.deadzone_degrees);
                const float vertical = remove_deadzone(wrap_degrees(roll -
                    context->neutral_roll), context->config.deadzone_degrees);
                if (context->was_scrolling != scroll)
                {
                    /* A fraction accumulated for pointer motion must not later
                     * reappear when returning from wheel mode, or vice versa.
                     */
                    mouse_motion_clear_residuals(context);
                }
                context->was_scrolling = scroll;
                if (scroll)
                {
                    /* Wheel units are detents; pointer axes stay zero in this mode. */
                    report->wheel = quantize(-vertical *
                        context->config.wheel_steps_per_second_per_degree *
                        elapsed_seconds, &context->residual_wheel);
                }
                else
                {
                    /* Preserve the original board's -pitch/+roll screen mapping.
                     * Convert bounded speed to per-report displacement using dt.
                     */
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
