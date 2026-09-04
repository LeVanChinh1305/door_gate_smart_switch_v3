#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_nvs.h"

#include "app_logic_relay.h"
#include "app_logic_touch.h"
#include "app_logic_led.h"
#include "app_logic_buzzer.h"


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
}