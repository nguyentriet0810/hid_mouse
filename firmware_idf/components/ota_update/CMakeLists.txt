idf_component_register(SRCS "ota_update.c" "ota_network.c" INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_common
    PRIV_REQUIRES ota_policy board_hal app_update esp_http_client
        esp_wifi esp_netif esp_event esp_timer nvs_flash mbedtls freertos)
