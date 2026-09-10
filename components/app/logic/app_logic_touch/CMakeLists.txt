idf_component_register(
    SRCS "app_logic_touch.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        app_touch 
        freertos 
        log 
        common 
        app_logic_relay 
        app_led_state 
        app_device_state 
        app_blufi 
        app_mqtt 
        app_logic_extra_config
)