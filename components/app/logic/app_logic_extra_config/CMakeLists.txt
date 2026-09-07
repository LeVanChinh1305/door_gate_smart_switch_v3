idf_component_register(
    SRCS "app_logic_extra_config.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        json          
        app_nvs     
        common
        app_logic_led
)