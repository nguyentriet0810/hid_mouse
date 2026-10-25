/* SPDX-License-Identifier: MIT */
/**
 * @file mpu6050.h
 * @brief Bounded I2C access to MPU6050 acceleration and angular-rate samples.
 *
 * This driver converts raw register words into physical units only. Calibration,
 * attitude filtering and cursor policy belong to mouse_motion.
 */
#ifndef MPU6050_H
#define MPU6050_H

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "driver/i2c_master.h"
#include "esp_err.h"
#pragma GCC diagnostic pop
#include "mouse_types.h"

typedef struct
{
    /** Device registration owned by this context; NULL means detached. */
    i2c_master_dev_handle_t device;
    /** Detected 7-bit address (0x68/0x69), or zero after successful teardown. */
    uint8_t address;
} mpu6050_t;

/**
 * @brief Discover the sensor and configure 100 Hz, +/-2 g, +/-500 degrees/s.
 * @param context Zero-initialized device context, exclusively owned by one task.
 * @param bus Borrowed initialized bus; the driver does not own/delete it.
 * @return ESP_OK on success, an argument/state error, or an I2C/device
 *         validation error. Failed setup attempts detach the device if possible.
 * @note Blocks for reset/wake stabilization delays and bounded I2C transfers.
 */
esp_err_t mpu6050_init(mpu6050_t * const context, i2c_master_bus_handle_t bus);

/**
 * @brief Read all axes in one burst and convert to g and degrees/second.
 * @param context Successfully initialized context owned by the calling task.
 * @param sample Output sample; unchanged if the I2C transaction fails.
 * @return ESP_OK or an argument/state/I2C error. No retry or filtering occurs.
 */
esp_err_t mpu6050_read(const mpu6050_t * const context,
                     mouse_imu_sample_t * const sample);

/**
 * @brief Detach this sensor so the application can retry discovery later.
 * @param context Initialized or already-detached context; must not be NULL.
 * @return ESP_OK for successful/already-complete teardown, otherwise the SDK
 *         error. On failure the handle is retained so cleanup can be retried.
 * @note The shared bus remains allocated and available to its owner.
 */
esp_err_t mpu6050_deinit(mpu6050_t * const context);

#endif
