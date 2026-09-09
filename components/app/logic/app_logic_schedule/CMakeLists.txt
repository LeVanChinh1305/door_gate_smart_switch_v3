idf_component_register(
    SRCS "app_logic_schedule.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_common log app_nvs app_logic_relay app_mqtt
)