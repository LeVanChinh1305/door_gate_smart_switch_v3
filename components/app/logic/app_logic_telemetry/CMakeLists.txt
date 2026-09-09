idf_component_register(
    SRCS "app_logic_telemetry.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        app_mqtt 
        json
)