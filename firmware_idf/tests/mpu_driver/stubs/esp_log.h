#ifndef TEST_ESP_LOG_H
#define TEST_ESP_LOG_H
void test_esp_log(const char *tag, const char *format, ...);
#define ESP_LOGE(tag, ...) test_esp_log((tag), __VA_ARGS__)
#define ESP_LOGW(tag, ...) test_esp_log((tag), __VA_ARGS__)
#define ESP_LOGI(tag, ...) test_esp_log((tag), __VA_ARGS__)
#define ESP_LOGD(tag, ...) test_esp_log((tag), __VA_ARGS__)
#endif
