idf_component_register(SRCS "ble_mouse.c" INCLUDE_DIRS "include"
    REQUIRES mouse_types esp_event PRIV_REQUIRES bt freertos)
