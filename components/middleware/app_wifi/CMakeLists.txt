idf_component_register(
    SRCS "app_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event esp_netif nvs_flash freertos app_nvs app_blufi app_device_state log
)