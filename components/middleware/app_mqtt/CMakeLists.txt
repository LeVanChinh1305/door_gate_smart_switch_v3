idf_component_register(SRCS "app_mqtt.c"
                       INCLUDE_DIRS "include"
                       REQUIRES mqtt nvs_flash app_nvs common)