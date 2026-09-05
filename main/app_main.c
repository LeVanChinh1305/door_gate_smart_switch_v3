#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_nvs.h"

#include "app_logic_relay.h"
#include "app_logic_touch.h"
#include "app_logic_led.h"
#include "app_logic_buzzer.h"
#include "app_wifi.h"
#include "app_blufi.h"
#include "app_device_state.h"


static const char *TAG = "APP_MAIN";
void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(10000));

    

    ESP_LOGI(TAG, "=== BẮT ĐẦU KIỂM TRA KHỞI TẠO NVS ===");

    // 1. Khởi tạo NVS
    esp_err_t eRet = app_nvs_InitNvs();
    if (eRet != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo NVS thất bại! Mã lỗi: %s", esp_err_to_name(eRet));
        return;
    }
    ESP_LOGI(TAG, "Khởi tạo NVS thành công!");
    // Khởi tạo các module nghiệp vụ ở tầng Application (Các hàm này sẽ tự gọi Driver bên dưới và tự xTaskCreate)
    eRet = app_logic_relay_Init();
    if (eRet != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo logic relay thất bại! Mã lỗi: %s", esp_err_to_name(eRet));
        return;
    }

    eRet = app_logic_touch_Init();
    if (eRet != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo logic cảm ứng thất bại! Mã lỗi: %s", esp_err_to_name(eRet));
        return;
    }

    eRet = app_logic_led_Init();
    if (eRet != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo logic LED thất bại! Mã lỗi: %s", esp_err_to_name(eRet));
        return;
    }

    eRet = app_logic_buzzer_Init();
    if (eRet != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo logic buzzer thất bại! Mã lỗi: %s", esp_err_to_name(eRet));
        return;
    }

    eRet = app_wifi_InitSta();
    if (eRet != ESP_OK) {
        ESP_LOGW(TAG, "Khởi tạo Wi-Fi STA gặp sự cố, kiểm tra trạng thái thiết bị...");
    }

    // Bước 4: Điều phối luồng khởi động dựa trên trạng thái thực tế của thiết bị (device.h)
    device_mode_t eCurrentMode = get_current_door_mode(); 
    
    switch (eCurrentMode) {
        case DEVICE_MODE_UNCONNECTED: 
            ESP_LOGI(TAG, "Thiết bị đang ở chế độ UNCONNECTED, kiểm tra NVS cấu hình Wi-Fi...");
            if (!app_nvs_IsProvisionedWifiConfig()) {
                ESP_LOGI(TAG, "Chưa có Wi-Fi trong NVS, chuyển sang chế độ tự động BluFi...");
                set_current_door_mode(DEVICE_MODE_CONNECT_AUTO); 
                (void)app_blufi_Init();
            } else {
                ESP_LOGI(TAG, "Đã có sẵn cấu hình Wi-Fi, chuyển sang trạng thái Normal...");
                set_current_door_mode(DEVICE_MODE_NORMAL); 
            }
            break;

        case DEVICE_MODE_CONNECT_AUTO: 
            ESP_LOGI(TAG, "Thiết bị đang ở chế độ kết nối tự động (BluFi)...");
            (void)app_blufi_Init();
            break;

        case DEVICE_MODE_CONNECT_MANUAL: 
            ESP_LOGI(TAG, "Thiết bị đang ở chế độ kết nối thủ công (UDP)...");
            // Triển khai logic lắng nghe cấu hình qua cổng UDP tại đây
            break;

        case DEVICE_MODE_NORMAL: 
            ESP_LOGI(TAG, "Thiết bị đang ở chế độ hoạt động bình thường, chờ kết nối mạng...");
            if (app_wifi_WaitForConnect(10000U)) {
                ESP_LOGI(TAG, "Kết nối Wi-Fi thành công!");
            } else {
                ESP_LOGW(TAG, "Timeout chờ kết nối Wi-Fi, tiếp tục chạy các task nền.");
            }
            break;

        default:
            ESP_LOGW(TAG, "Trạng thái không xác định, đưa về UNCONNECTED");
            set_current_door_mode(DEVICE_MODE_UNCONNECTED); 
            break;
    }

    ESP_LOGI(TAG, "=== HỆ THỐNG ĐÃ KHỞI ĐỘNG HOÀN TẤT ===");

    // Vòng lặp chính của app_main (giữ task chính hoạt động)
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000U));
    }
}