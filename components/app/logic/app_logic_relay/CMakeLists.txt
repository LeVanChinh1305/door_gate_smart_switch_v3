idf_component_register(
    SRCS "app_logic_relay.c"
    INCLUDE_DIRS "include"
    REQUIRES app_relay app_relay_state freertos log common
)