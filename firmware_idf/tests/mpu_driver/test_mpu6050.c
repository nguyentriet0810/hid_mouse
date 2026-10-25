/* Host regression tests compile the real driver against a fake register device.
 * No firmware is simulated beyond the I2C/RTOS/log adapter calls declared here.
 */
#include "mpu6050.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define REG_COUNT 128U
#define NO_REGISTER UINT16_C(0xFFFF)
#define IDENTITY_REG UINT8_C(0x75)
#define POWER_REG UINT8_C(0x6B)

struct test_i2c_device_s
{
    bool present;
    bool registered;
    uint8_t registers[REG_COUNT];
    uint8_t identities[3];
    esp_err_t identity_errors[3];
    uint8_t identity_sequence_length;
    uint8_t identity_reads;
    uint32_t writes;
    uint32_t register_writes[REG_COUNT];
    uint32_t readbacks;
};

struct test_i2c_bus_s
{
    struct test_i2c_device_s devices[2];
    uint32_t probes;
    uint32_t removed;
    uint32_t delayed_ticks;
    uint16_t fail_write_register;
    uint16_t fail_read_register;
    uint16_t corrupt_read_register;
    bool removal_fails;
};

static struct test_i2c_bus_s s_bus;
static uint32_t s_groups = 0U;

static void reset_bus(void)
{
    const struct test_i2c_bus_s initial = {0};
    s_bus = initial;
    s_bus.fail_write_register = NO_REGISTER;
    s_bus.fail_read_register = NO_REGISTER;
    s_bus.corrupt_read_register = NO_REGISTER;
    for (uint8_t index = 0U; index < 2U; ++index)
    {
        s_bus.devices[index].registers[IDENTITY_REG] = UINT8_C(0x68);
        s_bus.devices[index].registers[POWER_REG] = UINT8_C(0x40);
    }
}

const char *esp_err_to_name(esp_err_t error)
{
    const char *name = "TEST_ERROR";
    if (error == ESP_OK)
    {
        name = "ESP_OK";
    }
    return name;
}

void test_esp_log(const char *tag, const char *format, ...)
{
    /* Consume fixed arguments so normal diagnostics remain compiled by the tests. */
    assert(tag != NULL);
    assert(format != NULL);
}

void vTaskDelay(TickType_t ticks)
{
    assert(ticks > 0U);
    s_bus.delayed_ticks += ticks;
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout_ms)
{
    assert(bus == &s_bus);
    assert(timeout_ms > 0);
    ++s_bus.probes;
    const bool valid_address = (address >= UINT16_C(0x68)) && (address <= UINT16_C(0x69));
    return (valid_address && s_bus.devices[address - UINT16_C(0x68)].present) ?
        ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
    const i2c_device_config_t *configuration, i2c_master_dev_handle_t *device)
{
    assert(bus == &s_bus);
    assert(configuration != NULL && device != NULL);
    assert(configuration->device_address >= UINT16_C(0x68));
    assert(configuration->device_address <= UINT16_C(0x69));
    assert(configuration->scl_speed_hz == UINT32_C(100000));
    struct test_i2c_device_s * const selected =
        &s_bus.devices[configuration->device_address - UINT16_C(0x68)];
    assert(selected->present && !selected->registered);
    selected->registered = true;
    *device = selected;
    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    assert(device != NULL && device->registered);
    esp_err_t result = ESP_FAIL;
    if (!s_bus.removal_fails)
    {
        device->registered = false;
        ++s_bus.removed;
        result = ESP_OK;
    }
    return result;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
    const uint8_t *data, size_t length, int timeout_ms)
{
    assert(device != NULL && device->registered);
    assert(data != NULL && length == 2U && timeout_ms > 0);
    const uint8_t address = data[0];
    assert(address < REG_COUNT);
    if ((uint16_t)address == s_bus.fail_write_register)
    {
        return ESP_ERR_TIMEOUT;
    }
    ++device->writes;
    ++device->register_writes[address];
    if ((address == POWER_REG) && (data[1] == UINT8_C(0x80)))
    {
        /* A real reset restores sleep/config defaults but not the chip identity. */
        const uint8_t identity = device->registers[IDENTITY_REG];
        (void)memset(device->registers, 0, sizeof(device->registers));
        device->registers[IDENTITY_REG] = identity;
        device->registers[POWER_REG] = UINT8_C(0x40);
    }
    else
    {
        device->registers[address] = data[1];
    }
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
    const uint8_t *write_data, size_t write_length,
    uint8_t *read_data, size_t read_length, int timeout_ms)
{
    assert(device != NULL && device->registered);
    assert(write_data != NULL && write_length == 1U);
    assert(read_data != NULL && read_length > 0U && timeout_ms > 0);
    const uint8_t address = write_data[0];
    assert(((size_t)address + read_length) <= REG_COUNT);
    if ((uint16_t)address == s_bus.fail_read_register)
    {
        return ESP_ERR_TIMEOUT;
    }
    if (address == IDENTITY_REG)
    {
        assert(read_length == 1U);
        const uint8_t attempt = device->identity_reads;
        ++device->identity_reads;
        if (attempt < device->identity_sequence_length)
        {
            if (device->identity_errors[attempt] != ESP_OK)
            {
                return device->identity_errors[attempt];
            }
            read_data[0] = device->identities[attempt];
            return ESP_OK;
        }
    }
    (void)memcpy(read_data, &device->registers[address], read_length);
    if (address != IDENTITY_REG && read_length == 1U)
    {
        ++device->readbacks;
    }
    if ((uint16_t)address == s_bus.corrupt_read_register)
    {
        read_data[0] ^= UINT8_C(0x01);
    }
    return ESP_OK;
}

static void test_known_identity_at_both_addresses(void)
{
    for (uint8_t index = 0U; index < 2U; ++index)
    {
        reset_bus();
        s_bus.devices[index].present = true;
        mpu6050_t sensor = {0};
        assert(mpu6050_init(&sensor, &s_bus) == ESP_OK);
        assert(sensor.address == (uint8_t)(UINT8_C(0x68) + index));
        assert(sensor.device == &s_bus.devices[index]);
#if !defined(TEST_BASELINE_DRIVER)
        assert(sensor.identity == UINT8_C(0x68));
        assert(strcmp(mpu6050_model_name(&sensor), "MPU6050") == 0);
#endif
        assert(sensor.device->registers[POWER_REG] == UINT8_C(1));
        assert(sensor.device->registers[UINT8_C(0x1A)] == UINT8_C(3));
        assert(sensor.device->registers[UINT8_C(0x19)] == UINT8_C(9));
        assert(sensor.device->registers[UINT8_C(0x1B)] == UINT8_C(8));
        assert(sensor.device->registers[UINT8_C(0x1C)] == UINT8_C(0));
        assert(sensor.device->register_writes[UINT8_C(0x1D)] == 0U);
        assert(s_bus.delayed_ticks >= UINT32_C(110));
        assert(mpu6050_deinit(&sensor) == ESP_OK);
        assert(sensor.device == NULL && sensor.address == 0U);
#if !defined(TEST_BASELINE_DRIVER)
        assert(sensor.identity == 0U);
#endif
    }
    ++s_groups;
}

static void test_unknown_identity_is_never_configured(void)
{
    reset_bus();
    s_bus.devices[0].present = true;
    s_bus.devices[0].registers[IDENTITY_REG] = UINT8_C(0x71);
    mpu6050_t sensor = {0};
    assert(mpu6050_init(&sensor, &s_bus) == ESP_ERR_INVALID_RESPONSE);
    assert(sensor.device == NULL && sensor.address == 0U);
    assert(s_bus.devices[0].writes == 0U);
    assert(!s_bus.devices[0].registered);
    ++s_groups;
}

static void test_fallback_after_foreign_device(void)
{
    reset_bus();
    s_bus.devices[0].present = true;
    s_bus.devices[1].present = true;
    s_bus.devices[0].registers[IDENTITY_REG] = UINT8_C(0x71);
    mpu6050_t sensor = {0};
    assert(mpu6050_init(&sensor, &s_bus) == ESP_OK);
    assert(sensor.address == UINT8_C(0x69));
    assert(s_bus.devices[0].writes == 0U && !s_bus.devices[0].registered);
    assert(mpu6050_deinit(&sensor) == ESP_OK);
    ++s_groups;
}

static void test_missing_sensor(void)
{
    reset_bus();
    mpu6050_t sensor = {0};
    assert(mpu6050_init(&sensor, &s_bus) == ESP_ERR_NOT_FOUND);
    assert(sensor.device == NULL && sensor.address == 0U);
    assert(s_bus.probes == 2U);
    ++s_groups;
}

static void test_write_error_preserved(void)
{
    reset_bus();
    s_bus.devices[0].present = true;
    s_bus.fail_write_register = UINT16_C(0x1A);
    mpu6050_t sensor = {0};
    assert(mpu6050_init(&sensor, &s_bus) == ESP_ERR_TIMEOUT);
    assert(sensor.device == NULL && !s_bus.devices[0].registered);
    ++s_groups;
}

static void test_failed_cleanup_retains_handle(void)
{
    reset_bus();
    s_bus.devices[0].present = true;
    s_bus.devices[0].registers[IDENTITY_REG] = UINT8_C(0x71);
    s_bus.removal_fails = true;
    mpu6050_t sensor = {0};
    assert(mpu6050_init(&sensor, &s_bus) == ESP_FAIL);
    assert(sensor.device == &s_bus.devices[0] && sensor.device->registered);
    s_bus.removal_fails = false;
    assert(mpu6050_deinit(&sensor) == ESP_OK);
    ++s_groups;
}

static void set_word(uint8_t address, int32_t value)
{
    const uint32_t encoded = (uint32_t)((value < 0) ? (value + INT32_C(65536)) : value);
    s_bus.devices[0].registers[address] = (uint8_t)(encoded >> 8U);
    s_bus.devices[0].registers[(uint8_t)(address + 1U)] = (uint8_t)encoded;
}

static void test_samples_and_read_failure(void)
{
    reset_bus();
    s_bus.devices[0].present = true;
    mpu6050_t sensor = {0};
    assert(mpu6050_init(&sensor, &s_bus) == ESP_OK);
    set_word(UINT8_C(0x3B), INT32_C(16384));
    set_word(UINT8_C(0x3D), -INT32_C(16384));
    set_word(UINT8_C(0x3F), INT32_C(32767));
    set_word(UINT8_C(0x43), INT32_C(6550));
    set_word(UINT8_C(0x45), -INT32_C(6550));
    set_word(UINT8_C(0x47), -INT32_C(32768));
    mouse_imu_sample_t sample = {0};
    assert(mpu6050_read(&sensor, &sample) == ESP_OK);
    assert(fabsf(sample.accel_x_g - 1.0F) < 0.0001F);
    assert(fabsf(sample.accel_y_g + 1.0F) < 0.0001F);
    assert(fabsf(sample.accel_z_g - (32767.0F / 16384.0F)) < 0.0001F);
    assert(fabsf(sample.gyro_x_dps - 100.0F) < 0.0001F);
    assert(fabsf(sample.gyro_y_dps + 100.0F) < 0.0001F);
    assert(fabsf(sample.gyro_z_dps - (-32768.0F / 65.5F)) < 0.0001F);
    const mouse_imu_sample_t previous = sample;
    s_bus.fail_read_register = UINT16_C(0x3B);
    assert(mpu6050_read(&sensor, &sample) == ESP_ERR_TIMEOUT);
    assert(sample.accel_x_g == previous.accel_x_g && sample.accel_y_g == previous.accel_y_g);
    assert(sample.accel_z_g == previous.accel_z_g && sample.gyro_x_dps == previous.gyro_x_dps);
    assert(sample.gyro_y_dps == previous.gyro_y_dps && sample.gyro_z_dps == previous.gyro_z_dps);
    assert(mpu6050_deinit(&sensor) == ESP_OK);
    ++s_groups;
}

#if !defined(TEST_BASELINE_DRIVER)
static void test_transient_identity_failures_recover(void)
{
    for (uint8_t transport_error = 0U; transport_error < 2U; ++transport_error)
    {
        reset_bus();
        s_bus.devices[0].present = true;
        s_bus.devices[0].identity_sequence_length = 2U;
        s_bus.devices[0].identities[0] = UINT8_C(0x71);
        s_bus.devices[0].identities[1] = UINT8_C(0x68);
        if (transport_error != 0U)
        {
            s_bus.devices[0].identity_errors[0] = ESP_ERR_TIMEOUT;
        }
        mpu6050_t sensor = {0};
        assert(mpu6050_init(&sensor, &s_bus) == ESP_OK);
        assert(s_bus.devices[0].identity_reads >= 2U && s_bus.devices[0].identity_reads <= 3U);
        assert(mpu6050_deinit(&sensor) == ESP_OK);
    }
    ++s_groups;
}

static void test_bad_readback_rejected(void)
{
    reset_bus();
    s_bus.devices[0].present = true;
    s_bus.corrupt_read_register = UINT16_C(0x1A);
    mpu6050_t sensor = {0};
    assert(mpu6050_init(&sensor, &s_bus) != ESP_OK);
    assert(sensor.device == NULL && !s_bus.devices[0].registered);
    ++s_groups;
}

static void test_configuration_was_read_back(void)
{
    reset_bus();
    s_bus.devices[0].present = true;
    mpu6050_t sensor = {0};
    assert(mpu6050_init(&sensor, &s_bus) == ESP_OK);
    assert(s_bus.devices[0].readbacks >= 5U);
    assert(mpu6050_deinit(&sensor) == ESP_OK);
    ++s_groups;
}

static void test_mpu6500_profile(void)
{
    reset_bus();
    s_bus.devices[0].present = true;
    s_bus.devices[0].registers[IDENTITY_REG] = UINT8_C(0x70);
    mpu6050_t sensor = {0};
    assert(mpu6050_init(&sensor, &s_bus) == ESP_OK);
    assert(sensor.identity == UINT8_C(0x70));
    assert(strcmp(mpu6050_model_name(&sensor), "MPU6500") == 0);
    assert(sensor.device->register_writes[UINT8_C(0x1D)] == 1U);
    assert(sensor.device->registers[UINT8_C(0x1D)] == UINT8_C(0x03));
    assert(sensor.device->readbacks >= 6U);
    set_word(UINT8_C(0x3B), INT32_C(16384));
    set_word(UINT8_C(0x43), INT32_C(6550));
    mouse_imu_sample_t sample = {0};
    assert(mpu6050_read(&sensor, &sample) == ESP_OK);
    assert(fabsf(sample.accel_x_g - 1.0F) < 0.0001F);
    assert(fabsf(sample.gyro_x_dps - 100.0F) < 0.0001F);
    assert(mpu6050_deinit(&sensor) == ESP_OK);
    assert(sensor.device == NULL && sensor.address == 0U && sensor.identity == 0U);
    ++s_groups;
}

static void test_mpu6500_filter_readback_rejected(void)
{
    reset_bus();
    s_bus.devices[0].present = true;
    s_bus.devices[0].registers[IDENTITY_REG] = UINT8_C(0x70);
    s_bus.corrupt_read_register = UINT16_C(0x1D);
    mpu6050_t sensor = {0};
    assert(mpu6050_init(&sensor, &s_bus) != ESP_OK);
    assert(sensor.device == NULL && !s_bus.devices[0].registered);
    ++s_groups;
}

static void test_mpu6500_gyro_bypass_readback_rejected(void)
{
    reset_bus();
    s_bus.devices[0].present = true;
    s_bus.devices[0].registers[IDENTITY_REG] = UINT8_C(0x70);
    s_bus.corrupt_read_register = UINT16_C(0x1B);
    mpu6050_t sensor = {0};
    assert(mpu6050_init(&sensor, &s_bus) != ESP_OK);
    assert(sensor.device == NULL && !s_bus.devices[0].registered);
    ++s_groups;
}

static void test_model_name_boundaries(void)
{
    mpu6050_t sensor = {0};
    assert(strcmp(mpu6050_model_name(NULL), "unknown") == 0);
    assert(strcmp(mpu6050_model_name(&sensor), "unknown") == 0);
    sensor.identity = UINT8_C(0x71);
    assert(strcmp(mpu6050_model_name(&sensor), "unknown") == 0);
    sensor.identity = UINT8_C(0x68);
    assert(strcmp(mpu6050_model_name(&sensor), "MPU6050") == 0);
    sensor.identity = UINT8_C(0x70);
    assert(strcmp(mpu6050_model_name(&sensor), "MPU6500") == 0);
    ++s_groups;
}
#endif

int main(void)
{
    test_known_identity_at_both_addresses();
    test_unknown_identity_is_never_configured();
    test_fallback_after_foreign_device();
    test_missing_sensor();
    test_write_error_preserved();
    test_failed_cleanup_retains_handle();
    test_samples_and_read_failure();
#if !defined(TEST_BASELINE_DRIVER)
    test_transient_identity_failures_recover();
    test_bad_readback_rejected();
    test_configuration_was_read_back();
    test_mpu6500_profile();
    test_mpu6500_filter_readback_rejected();
    test_mpu6500_gyro_bypass_readback_rejected();
    test_model_name_boundaries();
#endif
    (void)printf("PASS: %lu real MPU driver test groups\n", (unsigned long)s_groups);
    return 0;
}
