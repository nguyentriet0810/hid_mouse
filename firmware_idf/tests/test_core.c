/* SPDX-License-Identifier: MIT
 * Host tests are verification code, outside the production MISRA scope.
 */
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "mouse_input.h"
#include "mouse_motion.h"
#include "test_ota_policy.h"

static const mouse_motion_config_t CONFIGURATION = {
    .deadzone_degrees = 6.0F,
    .pixels_per_second_per_degree = 45.0F,
    .maximum_pixels_per_second = 1300.0F,
    .wheel_steps_per_second_per_degree = 0.5F,
    .invert_x = false,
    .invert_y = false
};
static const mouse_imu_sample_t LEVEL = {
    .accel_x_g = 0.0F, .accel_y_g = 0.0F, .accel_z_g = 1.0F,
    .gyro_x_dps = 0.0F, .gyro_y_dps = 0.0F, .gyro_z_dps = 0.0F
};
static uint32_t s_tests = 0U;

static void calibrated(mouse_motion_t * const motion)
{
    mouse_report_t report = {0};
    assert(mouse_motion_init(motion, &CONFIGURATION));
    for (uint16_t index = 0U; index < 199U; ++index)
    {
        assert(mouse_motion_update(motion, &LEVEL, 0.01F, false, &report) ==
               MOUSE_MOTION_CALIBRATING);
        assert(report.x == 0 && report.y == 0 && report.wheel == 0);
    }
    assert(mouse_motion_update(motion, &LEVEL, 0.01F, false, &report) == MOUSE_MOTION_READY);
    assert(motion->ready);
}

static void test_configuration(void)
{
    mouse_motion_t motion = {0};
    mouse_motion_config_t invalid = CONFIGURATION;
    invalid.deadzone_degrees = (float)NAN;
    assert(!mouse_motion_init(&motion, &invalid));
    invalid = CONFIGURATION;
    invalid.maximum_pixels_per_second = (float)INFINITY;
    assert(!mouse_motion_init(&motion, &invalid));
    assert(!mouse_motion_init(NULL, &CONFIGURATION));
    assert(!mouse_motion_init(&motion, NULL));
    ++s_tests;
}

static void test_stationary(void)
{
    mouse_motion_t motion = {0};
    mouse_report_t report = {0};
    calibrated(&motion);
    for (uint16_t index = 0U; index < 1000U; ++index)
    {
        assert(mouse_motion_update(&motion, &LEVEL, 0.01F, false, &report) == MOUSE_MOTION_READY);
        assert(report.x == 0 && report.y == 0 && report.wheel == 0);
    }
    ++s_tests;
}

static void test_motion_rejected_during_calibration(void)
{
    mouse_motion_t motion = {0};
    mouse_report_t report = {0};
    mouse_imu_sample_t rotating = LEVEL;
    rotating.gyro_x_dps = 10.0F;
    assert(mouse_motion_init(&motion, &CONFIGURATION));
    for (uint16_t index = 0U; index < 199U; ++index)
    {
        assert(mouse_motion_update(&motion, &rotating, 0.01F, false, &report) ==
               MOUSE_MOTION_CALIBRATING);
    }
    assert(mouse_motion_update(&motion, &rotating, 0.01F, false, &report) ==
           MOUSE_MOTION_CALIBRATION_RETRY);
    assert(!motion.ready && motion.calibration_count == 0U);
    ++s_tests;
}

static void test_dead_sensor_rejected(void)
{
    mouse_motion_t motion = {0};
    mouse_report_t report = {0};
    const mouse_imu_sample_t zero = {0};
    assert(mouse_motion_init(&motion, &CONFIGURATION));
    for (uint16_t index = 0U; index < 199U; ++index)
    {
        (void)mouse_motion_update(&motion, &zero, 0.01F, false, &report);
    }
    assert(mouse_motion_update(&motion, &zero, 0.01F, false, &report) ==
           MOUSE_MOTION_CALIBRATION_RETRY);
    ++s_tests;
}

static void test_bad_samples_and_timing(void)
{
    mouse_motion_t motion = {0};
    mouse_report_t report = {0};
    mouse_imu_sample_t sample = LEVEL;
    calibrated(&motion);
    sample.accel_x_g = (float)NAN;
    assert(mouse_motion_update(&motion, &sample, 0.01F, false, &report) == MOUSE_MOTION_INVALID);
    sample = LEVEL;
    sample.gyro_z_dps = 502.0F;
    assert(mouse_motion_update(&motion, &sample, 0.01F, false, &report) == MOUSE_MOTION_INVALID);
    assert(mouse_motion_update(&motion, &LEVEL, 0.0F, false, &report) == MOUSE_MOTION_INVALID);
    assert(mouse_motion_update(&motion, &LEVEL, 0.1F, false, &report) == MOUSE_MOTION_INVALID);
    assert(mouse_motion_update(&motion, &LEVEL, (float)NAN, false, &report) == MOUSE_MOTION_INVALID);
    assert(mouse_motion_update(NULL, &LEVEL, 0.01F, false, &report) == MOUSE_MOTION_INVALID);
    ++s_tests;
}

static void test_tilt_scroll_and_recenter(void)
{
    mouse_motion_t motion = {0};
    mouse_report_t report = {0};
    mouse_imu_sample_t tilted = LEVEL;
    calibrated(&motion);
    tilted.accel_y_g = 0.5F;
    tilted.accel_z_g = 0.8660254F;
    for (uint16_t index = 0U; index < 500U; ++index)
    {
        assert(mouse_motion_update(&motion, &tilted, 0.01F, false, &report) == MOUSE_MOTION_READY);
    }
    assert(report.x == 0 && report.y > 0 && report.y <= 13);
    int32_t wheel = 0;
    for (uint16_t index = 0U; index < 100U; ++index)
    {
        (void)mouse_motion_update(&motion, &tilted, 0.01F, true, &report);
        assert(report.x == 0 && report.y == 0);
        wheel += report.wheel;
    }
    assert(wheel < 0);
    mouse_motion_recenter(&motion);
    (void)mouse_motion_update(&motion, &tilted, 0.01F, false, &report);
    assert(report.x == 0 && report.y == 0 && report.wheel == 0);
    ++s_tests;
}

static void test_fractional_motion_and_horizontal_direction(void)
{
    mouse_motion_t motion = {0};
    mouse_report_t report = {0};
    mouse_imu_sample_t tilted = LEVEL;
    calibrated(&motion);
    /* -7 degree accel pitch produces +X, initially below one pixel/tick. */
    tilted.accel_x_g = 0.1218693F;
    tilted.accel_z_g = 0.9925462F;
    int32_t total_x = 0;
    for (uint16_t index = 0U; index < 1000U; ++index)
    {
        (void)mouse_motion_update(&motion, &tilted, 0.01F, false, &report);
        total_x += report.x;
        assert(report.y == 0 && report.wheel == 0);
    }
    assert(total_x > 100);
    ++s_tests;
}

static void test_gyro_bias(void)
{
    mouse_motion_t motion = {0};
    mouse_report_t report = {0};
    mouse_imu_sample_t sample = LEVEL;
    sample.gyro_x_dps = 1.25F;
    assert(mouse_motion_init(&motion, &CONFIGURATION));
    for (uint16_t index = 0U; index < 200U; ++index)
    {
        (void)mouse_motion_update(&motion, &sample, 0.01F, false, &report);
    }
    assert(motion.ready && fabsf(motion.gyro_bias[0] - 1.25F) < 0.001F);
    for (uint16_t index = 0U; index < 500U; ++index)
    {
        (void)mouse_motion_update(&motion, &sample, 0.01F, false, &report);
        assert(report.x == 0 && report.y == 0);
    }
    ++s_tests;
}

static void expect_buttons(mouse_input_t * const input, uint8_t expected)
{
    uint8_t buttons = 0xFFU;
    assert(mouse_input_peek(input, &buttons));
    assert(buttons == expected);
    mouse_input_ack(input);
}

static void test_debounce_and_drag(void)
{
    mouse_input_t input = {0};
    mouse_input_update(&input, 0U, 0U, true);
    expect_buttons(&input, 0U);
    mouse_input_update(&input, 1U, 10U, true);
    mouse_input_update(&input, 0U, 20U, true);
    mouse_input_update(&input, 1U, 30U, true);
    mouse_input_update(&input, 1U, 49U, true);
    assert(input.count == 0U);
    mouse_input_update(&input, 1U, 50U, true);
    expect_buttons(&input, 1U);
    mouse_input_update(&input, 1U, 1000U, true);
    assert(input.stable_keys == 1U && input.count == 0U);
    mouse_input_update(&input, 0U, 1010U, true);
    mouse_input_update(&input, 0U, 1030U, true);
    expect_buttons(&input, 0U);
    ++s_tests;
}

static void test_click_edges_survive_backpressure(void)
{
    mouse_input_t input = {0};
    mouse_input_update(&input, 0U, 0U, true);
    expect_buttons(&input, 0U);
    mouse_input_update(&input, 1U, 10U, true);
    mouse_input_update(&input, 1U, 30U, true);
    mouse_input_update(&input, 0U, 40U, true);
    mouse_input_update(&input, 0U, 60U, true);
    mouse_input_update(&input, 1U, 70U, true);
    mouse_input_update(&input, 1U, 90U, true);
    mouse_input_update(&input, 0U, 100U, true);
    mouse_input_update(&input, 0U, 120U, true);
    assert(input.count == 4U);
    expect_buttons(&input, 1U);
    expect_buttons(&input, 0U);
    expect_buttons(&input, 1U);
    expect_buttons(&input, 0U);
    ++s_tests;
}

static void test_disconnect_clears_events_and_rearms_after_release(void)
{
    mouse_input_t input = {0};
    mouse_input_update(&input, 1U, 0U, true);
    mouse_input_update(&input, 1U, 20U, true);
    assert(!input.armed && input.count == 0U);
    mouse_input_update(&input, 0U, 30U, true);
    mouse_input_update(&input, 0U, 50U, true);
    expect_buttons(&input, 0U);
    mouse_input_update(&input, 1U, 60U, true);
    mouse_input_update(&input, 1U, 80U, true);
    assert(input.count == 1U);
    mouse_input_update(&input, 1U, 90U, false);
    assert(input.count == 0U && !input.armed);
    mouse_input_update(&input, 1U, 100U, true);
    assert(!input.armed && input.count == 0U);
    ++s_tests;
}

static void test_mode_short_press_and_scroll(void)
{
    mouse_input_t input = {0};
    mouse_input_update(&input, 4U, 0U, true);
    mouse_input_update(&input, 4U, 20U, true);
    mouse_input_update(&input, 0U, 100U, true);
    mouse_input_update(&input, 0U, 120U, true);
    assert(input.recenter && !input.scroll);
    mouse_input_update(&input, 4U, 200U, true);
    mouse_input_update(&input, 4U, 220U, true);
    mouse_input_update(&input, 4U, 520U, true);
    assert(input.scroll && !input.recenter);
    mouse_input_update(&input, 0U, 530U, true);
    mouse_input_update(&input, 0U, 550U, true);
    assert(!input.scroll && !input.recenter);
    ++s_tests;
}

static void test_timer_wraparound(void)
{
    mouse_input_t input = {0};
    mouse_input_update(&input, 0U, UINT32_MAX - 20U, true);
    expect_buttons(&input, 0U);
    mouse_input_update(&input, 1U, UINT32_MAX - 10U, true);
    mouse_input_update(&input, 1U, 9U, true);
    expect_buttons(&input, 1U);
    ++s_tests;
}

static void test_overflow_forces_release(void)
{
    mouse_input_t input = {0};
    mouse_input_update(&input, 0U, 0U, true);
    expect_buttons(&input, 0U);
    uint32_t now = 0U;
    uint8_t keys = 0U;
    for (uint8_t index = 0U; index < 17U; ++index)
    {
        keys ^= 1U;
        now += 30U;
        mouse_input_update(&input, keys, now, true);
        mouse_input_update(&input, keys, now + 20U, true);
    }
    assert(input.overflow && !input.armed && input.count == 1U);
    expect_buttons(&input, 0U);
    ++s_tests;
}

int main(void)
{
    test_configuration();
    test_stationary();
    test_motion_rejected_during_calibration();
    test_dead_sensor_rejected();
    test_bad_samples_and_timing();
    test_tilt_scroll_and_recenter();
    test_fractional_motion_and_horizontal_direction();
    test_gyro_bias();
    test_debounce_and_drag();
    test_click_edges_survive_backpressure();
    test_disconnect_clears_events_and_rearms_after_release();
    test_mode_short_press_and_scroll();
    test_timer_wraparound();
    test_overflow_forces_release();
    s_tests += test_ota_policy();
    (void)printf("PASS: %lu core test groups\n", (unsigned long)s_tests);
    return 0;
}
