idf_component_register(
    SRCS "app_buzzer.c"
    INCLUDE_DIRS "include"
    REQUIRES driver log freertos common
)