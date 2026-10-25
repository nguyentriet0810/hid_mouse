/* SPDX-License-Identifier: MIT */
/**
 * @file mouse_motion.h
 * @brief Hardware-independent tilt-to-velocity filtering and calibration API.
 *
 * One application task owns each context. Initialize before updating and pass
 * sensor samples in g/degrees per second; this component does not access I2C,
 * schedule itself, or send BLE reports.
 */
#ifndef MOUSE_MOTION_H
#define MOUSE_MOTION_H

#include "mouse_types.h"

/** @brief Validated motion tuning, copied into the context during initialization. */
typedef struct
{
    float deadzone_degrees; /**< Neutral-angle half-width; range [0, 45] degrees. */
    float pixels_per_second_per_degree; /**< Pointer gain outside deadzone: (0, 200]. */
    float maximum_pixels_per_second; /**< Per-axis speed limit: (0, 10000]. */
    float wheel_steps_per_second_per_degree; /**< Wheel gain outside deadzone: (0, 10]. */
    bool invert_x; /**< Reverse the default -pitch-to-screen-X mapping. */
    bool invert_y; /**< Reverse the default +roll-to-screen-Y mapping. */
} mouse_motion_config_t;

/** @brief Caller-owned filter state; exposed for static allocation and health checks. */
typedef struct
{
    mouse_motion_config_t config; /**< Validated, immutable tuning until next init. */
    bool ready; /**< True only after a stationary calibration window passes. */
    uint16_t calibration_count; /**< Accepted samples in the current 200-sample window. */
    float sums[6]; /**< Sums ordered accel XYZ followed by gyro XYZ. */
    float squares[6]; /**< Sum of squares for per-axis stationary-variance tests. */
    float gyro_bias[3]; /**< Stationary XYZ angular-rate offsets, degrees/second. */
    float roll_degrees; /**< Current complementary-filter roll estimate. */
    float pitch_degrees; /**< Current complementary-filter pitch estimate. */
    float neutral_roll; /**< Captured roll corresponding to zero pointer velocity. */
    float neutral_pitch; /**< Captured pitch corresponding to zero pointer velocity. */
    float residual_x; /**< Sub-count horizontal displacement retained between reports. */
    float residual_y; /**< Sub-count vertical displacement retained between reports. */
    float residual_wheel; /**< Fractional wheel detents retained between reports. */
    bool was_scrolling; /**< Detect mode changes so fractional motion cannot leak modes. */
} mouse_motion_t;

/** @brief Result of one update; calibration statuses always produce zero motion. */
typedef enum
{
    MOUSE_MOTION_INVALID = 0, /**< Invalid pointers, non-finite/range-invalid sample or dt. */
    MOUSE_MOTION_CALIBRATING, /**< Still collecting a stationary sample window. */
    MOUSE_MOTION_CALIBRATION_RETRY, /**< Window failed; a fresh window starts next update. */
    MOUSE_MOTION_READY /**< Calibration just completed or a normal report was produced. */
} mouse_motion_status_t;

/**
 * @brief Validate tuning and reset all calibration/filter state.
 * @param[in,out] context Caller-owned storage; no prior initialization is required.
 * @param[in] config Finite tuning values within the documented field ranges.
 * @return True on success; false for NULL/invalid configuration, leaving context unchanged.
 */
bool mouse_motion_init(mouse_motion_t * const context,
                       const mouse_motion_config_t * const config);
/**
 * @brief Consume one sample and generate relative pointer or wheel increments.
 * @param[in,out] context Context successfully initialized by mouse_motion_init().
 * @param[in] sample Coherent accelerometer/gyro sample, in g and degrees/second.
 * @param[in] elapsed_seconds Actual positive sample interval, at most 0.05 seconds.
 * @param[in] scroll True routes roll-derived velocity to wheel instead of X/Y.
 * @param[in,out] report X/Y/wheel are cleared then populated; buttons are untouched.
 * @return Calibration progress, normal readiness, or invalid-input status.
 * @note Valid samples first fill a 200-sample stationary calibration window.
 *       Rejected sample values do not advance calibration or filter state. When
 *       pointers are valid, invalid input produces zero motion in the report.
 */
mouse_motion_status_t mouse_motion_update(mouse_motion_t * const context,
    const mouse_imu_sample_t * const sample, float elapsed_seconds,
    bool scroll, mouse_report_t * const report);
/**
 * @brief Capture the current filtered pose as neutral and discard fractional motion.
 * @param[in,out] context Owning task's filter state; NULL/unready contexts are ignored.
 * @note Does not recalibrate gyro bias or reset the attitude estimate.
 */
void mouse_motion_recenter(mouse_motion_t * const context);
/**
 * @brief Discard unsent fractional pointer/wheel displacement.
 * @param[in,out] context Owning task's filter state; NULL is ignored.
 * @note Use on connection/mode boundaries so old sub-count movement is not replayed.
 */
void mouse_motion_clear_residuals(mouse_motion_t * const context);

#endif
