idf_component_register(
    SRCS 
        "app_ble_manager.c"     
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        bt                      # Chứa NimBLE stack và BT Controller API
        log
)