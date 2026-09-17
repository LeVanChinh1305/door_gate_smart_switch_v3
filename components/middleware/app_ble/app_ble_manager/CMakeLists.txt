idf_component_register(
    SRCS 
        "app_ble_manager.c"     
    INCLUDE_DIRS 
        "include"                     # Thư mục chứa các file .h
    REQUIRES 
        bt                      # Bắt buộc: Chứa NimBLE stack và BT Controller API
        nvs_flash               # Lưu trữ NVS
        esp_wifi                # Quản lý Wi-Fi
        esp_event               # Hệ thống Event Loop
        json                    # cJSON library
)