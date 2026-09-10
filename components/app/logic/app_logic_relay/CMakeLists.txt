idf_component_register(
    SRCS "app_logic_relay.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        app_relay 
        app_relay_state 
        freertos 
        log common 
        app_logic_mqtt_publisher 
        app_nvs 
        app_logic_telemetry
        app_logic_extra_config
        app_led_state
)