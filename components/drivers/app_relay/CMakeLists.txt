idf_component_register(
    SRCS "app_relay.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio freertos
    )