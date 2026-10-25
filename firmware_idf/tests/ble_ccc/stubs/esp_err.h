#ifndef TEST_ESP_ERR_H
#define TEST_ESP_ERR_H
#include <stdint.h>
typedef int32_t esp_err_t;
#define ESP_OK INT32_C(0)
#define ESP_FAIL (-INT32_C(1))
#define ESP_ERR_INVALID_ARG INT32_C(0x102)
#define ESP_ERR_INVALID_RESPONSE INT32_C(0x108)
#define ESP_ERR_NVS_NOT_FOUND INT32_C(0x1102)
#endif
