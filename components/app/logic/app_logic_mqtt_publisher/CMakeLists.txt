idf_component_register(
    SRCS "app_logic_mqtt_publisher.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        mqtt    # Bắt buộc để sử dụng các hàm esp_mqtt_client_publish và mqtt_client.h
        log     # Để sử dụng thư viện in log ESP_LOGI, ESP_LOGE
        app_mqtt 
        mqtt_vconnex_decrypt
        json 
)