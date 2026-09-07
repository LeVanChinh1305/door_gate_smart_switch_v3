idf_component_register(
    SRCS "app_logic_touch.c"
    INCLUDE_DIRS "include"
    REQUIRES app_touch freertos log common 
)