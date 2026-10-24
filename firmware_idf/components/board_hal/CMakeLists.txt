idf_component_register(SRCS "board_hal.c" INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_i2c PRIV_REQUIRES esp_psram spi_flash)
