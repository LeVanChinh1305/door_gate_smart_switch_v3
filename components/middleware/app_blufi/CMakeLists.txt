idf_component_register(
    SRCS "app_blufi.c"
    INCLUDE_DIRS 
        "include"
    REQUIRES esp_wifi esp_event nvs_flash app_nvs app_wifi json app_update
    PRIV_REQUIRES bt
)