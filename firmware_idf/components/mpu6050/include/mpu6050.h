/* SPDX-License-Identifier: MIT */
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
    i2c_master_dev_handle_t device;
    uint8_t address;
} mpu6050_t;

/* Zero-initialize context. All calls belong to a single owning task. */
esp_err_t mpu6050_init(mpu6050_t * const context, i2c_master_bus_handle_t bus);
esp_err_t mpu6050_read(const mpu6050_t * const context,
                     mouse_imu_sample_t * const sample);
esp_err_t mpu6050_deinit(mpu6050_t * const context);

#endif
