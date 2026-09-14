idf_component_register(
    SRCS "app_udp.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event esp_netif esp_timer freertos lwip json app_nvs app_wifi app_device_state esp_hw_support log app_led_state
)
