idf_component_register(
    SRCS "app_ble_mesh.c"
    INCLUDE_DIRS "include"
    REQUIRES
        bt
        nvs_flash
        app_ble_manager
)