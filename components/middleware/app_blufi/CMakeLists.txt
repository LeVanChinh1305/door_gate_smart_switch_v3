idf_component_register(
    SRCS "app_blufi.c"
    INCLUDE_DIRS 
        "include"
    REQUIRES esp_wifi esp_event nvs_flash app_nvs app_wifi app_device_state json app_update log app_mqtt app_led_state
    PRIV_REQUIRES bt
)