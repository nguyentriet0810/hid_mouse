/* SPDX-License-Identifier: MIT */
#include "mpu6050.h"
#include <stddef.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

#define MPU_REG_SAMPLE_DIV UINT8_C(0x19)
#define MPU_REG_CONFIG UINT8_C(0x1A)
#define MPU_REG_GYRO_CONFIG UINT8_C(0x1B)
#define MPU_REG_ACCEL_CONFIG UINT8_C(0x1C)
#define MPU_REG_ACCEL_XOUT UINT8_C(0x3B)
#define MPU_REG_POWER UINT8_C(0x6B)
#define MPU_REG_WHO_AM_I UINT8_C(0x75)
#define MPU_TIMEOUT_MS INT32_C(25)

static esp_err_t write_register(const mpu6050_t * const context,
                                uint8_t address, uint8_t value)
{
    const uint8_t data[2] = {address, value};
    return i2c_master_transmit(context->device, data, sizeof(data), MPU_TIMEOUT_MS);
}

/* Decode through int32_t: no implementation-defined unsigned-to-int16 cast. */
static int32_t decode_signed(const uint8_t * const data)
{
    const uint32_t word = ((uint32_t)data[0] << 8U) | (uint32_t)data[1];
    int32_t value = (int32_t)word;
    if (word >= UINT32_C(32768))
    {
        value -= INT32_C(65536);
    }
    return value;
}

esp_err_t mpu6050_deinit(mpu6050_t * const context)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (context != NULL)
    {
        result = ESP_OK;
        if (context->device != NULL)
        {
            result = i2c_master_bus_rm_device(context->device);
            if (result == ESP_OK)
            {
                context->device = NULL;
                context->address = 0U;
            }
        }
    }
    return result;
}

esp_err_t mpu6050_init(mpu6050_t * const context, i2c_master_bus_handle_t bus)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if ((context != NULL) && (bus != NULL))
    {
        result = ESP_ERR_INVALID_STATE;
        if (context->device == NULL)
        {
            result = ESP_ERR_NOT_FOUND;
            for (uint16_t address = UINT16_C(0x68);
                 (address <= UINT16_C(0x69)) && (result != ESP_OK); ++address)
            {
                if (i2c_master_probe(bus, address, MPU_TIMEOUT_MS) == ESP_OK)
                {
                    const i2c_device_config_t device_config = {
                        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                        .device_address = address,
                        .scl_speed_hz = UINT32_C(100000),
                        .scl_wait_us = 0U,
                        .flags = {.disable_ack_check = 0U}
                    };
                    result = i2c_master_bus_add_device(bus, &device_config,
                                                      &context->device);
                    if (result == ESP_OK)
                    {
                        const uint8_t who_register = MPU_REG_WHO_AM_I;
                        uint8_t identity = 0U;
                        context->address = (uint8_t)address;
                        result = i2c_master_transmit_receive(context->device,
                            &who_register, 1U, &identity, 1U, MPU_TIMEOUT_MS);
                        if ((result == ESP_OK) && (identity != UINT8_C(0x68)))
                        {
                            result = ESP_ERR_INVALID_RESPONSE;
                        }
                        if (result != ESP_OK)
                        {
                            const esp_err_t cleanup = mpu6050_deinit(context);
                            if (cleanup != ESP_OK)
                            {
                                result = cleanup;
                                break;
                            }
                        }
                    }
                }
            }
            if (result == ESP_OK)
            {
                result = write_register(context, MPU_REG_POWER, UINT8_C(0x80));
            }
            if (result == ESP_OK)
            {
                vTaskDelay(pdMS_TO_TICKS(100U));
                result = write_register(context, MPU_REG_POWER, UINT8_C(0x01));
            }
            if (result == ESP_OK)
            {
                vTaskDelay(pdMS_TO_TICKS(10U));
                result = write_register(context, MPU_REG_CONFIG, UINT8_C(0x03));
            }
            if (result == ESP_OK)
            {
                /* DLPF enabled: 1 kHz / (1 + 9) = 100 Hz. */
                result = write_register(context, MPU_REG_SAMPLE_DIV, UINT8_C(9));
            }
            if (result == ESP_OK)
            {
                result = write_register(context, MPU_REG_GYRO_CONFIG, UINT8_C(0x08));
            }
            if (result == ESP_OK)
            {
                result = write_register(context, MPU_REG_ACCEL_CONFIG, UINT8_C(0));
            }
            if ((result != ESP_OK) && (context->device != NULL))
            {
                const esp_err_t cleanup = mpu6050_deinit(context);
                if (cleanup != ESP_OK)
                {
                    result = cleanup;
                }
            }
        }
    }
    return result;
}

esp_err_t mpu6050_read(const mpu6050_t * const context,
                     mouse_imu_sample_t * const sample)
{
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if ((context != NULL) && (sample != NULL))
    {
        result = ESP_ERR_INVALID_STATE;
        if (context->device != NULL)
        {
            const uint8_t start_register = MPU_REG_ACCEL_XOUT;
            uint8_t data[14] = {0U};
            result = i2c_master_transmit_receive(context->device,
                &start_register, 1U, data, sizeof(data), MPU_TIMEOUT_MS);
            if (result == ESP_OK)
            {
                sample->accel_x_g = (float)decode_signed(&data[0]) / 16384.0F;
                sample->accel_y_g = (float)decode_signed(&data[2]) / 16384.0F;
                sample->accel_z_g = (float)decode_signed(&data[4]) / 16384.0F;
                /* Bytes 6..7 are temperature, unused by the mouse. */
                sample->gyro_x_dps = (float)decode_signed(&data[8]) / 65.5F;
                sample->gyro_y_dps = (float)decode_signed(&data[10]) / 65.5F;
                sample->gyro_z_dps = (float)decode_signed(&data[12]) / 65.5F;
            }
        }
    }
    return result;
}
