idf_component_register(SRCS "app_main.c" INCLUDE_DIRS "."
    PRIV_REQUIRES board_hal mpu6050 mouse_motion mouse_input ble_mouse
                  nvs_flash esp_timer ota_update)
