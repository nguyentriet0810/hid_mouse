#ifndef TEST_I2C_MASTER_H
#define TEST_I2C_MASTER_H
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
struct test_i2c_bus_s;
struct test_i2c_device_s;
typedef struct test_i2c_bus_s *i2c_master_bus_handle_t;
typedef struct test_i2c_device_s *i2c_master_dev_handle_t;
typedef enum { I2C_ADDR_BIT_LEN_7 = 0 } i2c_addr_bit_len_t;
typedef struct
{
    i2c_addr_bit_len_t dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
    uint32_t scl_wait_us;
    struct { uint32_t disable_ack_check: 1; } flags;
} i2c_device_config_t;
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout_ms);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
    const i2c_device_config_t *configuration, i2c_master_dev_handle_t *device);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
    const uint8_t *data, size_t length, int timeout_ms);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
    const uint8_t *write_data, size_t write_length,
    uint8_t *read_data, size_t read_length, int timeout_ms);
#endif
