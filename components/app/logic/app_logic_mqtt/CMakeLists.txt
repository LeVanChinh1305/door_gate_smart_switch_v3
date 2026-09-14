idf_component_register(
    SRCS "app_logic_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        mqtt 
        json 
        app_mqtt 
        mqtt_vconnex_decrypt 
        common 
        app_logic_relay 
        app_led_state 
        app_logic_mqtt_publisher 
        app_logic_extra_config
        app_nvs
        esp_timer
        app_logic_telemetry
        app_ota
)