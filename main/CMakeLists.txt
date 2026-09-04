idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES app_nvs app_logic_relay app_logic_touch app_logic_led app_logic_buzzer freertos log
)
