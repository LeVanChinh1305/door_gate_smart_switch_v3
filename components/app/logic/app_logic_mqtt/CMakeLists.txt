idf_component_register(
    SRCS "app_logic_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt json app_mqtt mqtt_vconnex_decrypt common app_logic_relay
)