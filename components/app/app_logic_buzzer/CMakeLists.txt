idf_component_register(
    SRCS "app_logic_buzzer.c"
    INCLUDE_DIRS "include"
    REQUIRES app_buzzer freertos log
)