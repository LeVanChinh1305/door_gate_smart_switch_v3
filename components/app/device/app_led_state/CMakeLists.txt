idf_component_register(
    SRCS "app_led_state.c"
    INCLUDE_DIRS "include"
    REQUIRES app_led app_logic_led common
)