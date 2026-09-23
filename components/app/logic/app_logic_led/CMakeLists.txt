idf_component_register(
    SRCS "app_logic_led.c"
    INCLUDE_DIRS "include"
    REQUIRES app_led freertos log common
)