idf_component_register(
    SRCS "app_ota.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client app_update json app_mqtt common app_ble_mesh
)