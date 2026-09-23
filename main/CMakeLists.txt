idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES 
        app_nvs 
        app_logic_relay 
        app_logic_touch 
        app_logic_led 
        app_logic_buzzer 
        app_wifi 
        app_blufi 
        app_mqtt
        app_device_state  
        freertos 
        log 
        app_led_state
        app_relay_state
        app_logic_schedule
        app_logic_extra_config
        app_udp
        # app_ble_mesh
        app_logic_ble_ibeacon
)
