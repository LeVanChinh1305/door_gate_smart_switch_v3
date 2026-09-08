idf_component_register(
    SRCS "app_sntp.c"
    INCLUDE_DIRS "include"
    REQUIRES lwip esp_netif
)