#include "app_wifi.h"
#include "app_nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "stdbool.h"
#include "esp_log.h"
#include "nvs.h"
#include "blufi_app.h"
#include "device.h"


static const char *TAG = "APP_WIFI";
static int retry_sum = 0;
static EventGroupHandle_t s_wifi_event_group = NULL; 

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data){
    if(event_base == WIFI_EVENT){
        switch(event_id){
            case WIFI_EVENT_STA_START:
                if (app_nvs_IsProvisionedWifiConfig()) {
                    ESP_LOGI(TAG, "Wi-Fi STA đã khởi động, đang kết nối...");
                    esp_wifi_connect();
                } else {
                    ESP_LOGI(TAG, "Wi-Fi STA đã khởi động, đang chờ cấu hình BLE...");
                }
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Wi-Fi đã kết nối đến AP, đang chờ địa chỉ IP");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
            {
                wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
                if (event != NULL) {
                    ESP_LOGW(TAG, "Đã ngắt kết nối khỏi AP, mã nguyên nhân: %d", event->reason);
                } else {
                    ESP_LOGW(TAG, "Đã ngắt kết nối khỏi AP, không có thông tin nguyên nhân");
                }

                if (retry_sum < MAXIMUM_RETRY_CONNECT_WIFI) {
                    retry_sum++;
                    ESP_LOGI(TAG, "Đang thử kết nối lại %d/%d", retry_sum, MAXIMUM_RETRY_CONNECT_WIFI);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_wifi_connect();
                } else {
                    retry_sum = 0;
                    if (s_wifi_event_group != NULL) {
                        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    }
                    ESP_LOGE(TAG, "Không thể kết nối Wi-Fi sau %d lần thử", MAXIMUM_RETRY_CONNECT_WIFI);
                }
                break;
            }
            default:
                break; 
        }
    }else if(event_base == IP_EVENT){
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
            {

                ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
                if (event != NULL) {
                    ESP_LOGI(TAG, "Đã nhận địa chỉ IP: " IPSTR, IP2STR(&event->ip_info.ip));
                }
                retry_sum = 0;
                if (s_wifi_event_group != NULL) {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                }
                if (blufi_app_is_connected()) {
                    blufi_app_report_wifi_status(true);
                }
                break;
            }

            case IP_EVENT_STA_LOST_IP:
                ESP_LOGW(TAG, "Đã mất địa chỉ IP từ AP!");
                break;

            default:
                break;
        }
    }
}

esp_err_t app_wifi_init_sta(void){
    if(s_wifi_event_group == NULL){
        s_wifi_event_group = xEventGroupCreate(); 
        if(s_wifi_event_group == NULL){
            ESP_LOGE(TAG, "Khởi tạo event group thất bại");
            return ESP_ERR_NO_MEM; // Khong du bo nho 
        }
    }
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT); 
    esp_err_t ret = ESP_OK;
    // khởi tạo LWIP stack
    ret = esp_netif_init();  
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "Khởi tạo esp_netif thất bại: %s", esp_err_to_name(ret));
        return ret; 
    }

    // tạo event loop mặc định
    ret = esp_event_loop_create_default();  
    if(ret != ESP_OK && ret != ESP_ERR_INVALID_STATE){  // Lỗi hoặc là trạng thái không hợp lệ 
                                                        // không hợp lệ là đã khởi tạo trước đó (ý là chỉ được cho phép 1 event loop)
        ESP_LOGE(TAG, "Tạo event loop mặc định thất bại: %s", esp_err_to_name(ret));
    }

    // tạo đối tượng netif dạng sta 
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta(); 
    if(sta_netif == NULL){
        ESP_LOGE(TAG, "Tạo Wi-Fi STA mặc định thất bại");
        return ESP_FAIL;
    }

    // Khởi tạo wifi driver 
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "Khởi tạo esp_wifi thất bại: %s", esp_err_to_name(ret));
        return ret; 
    }

    // đăng ký sự kiện 
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL); 
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "Đăng ký sự kiện Wi-Fi thất bại: %s", esp_err_to_name(ret));
        return ret; 
    }
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL); 
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "Đăng ký sự kiện IP thất bại: %s", esp_err_to_name(ret));
        return ret; 
    }

    wifi_config_t wifi_cfg = {0}; 
    ret = app_nvs_LoadWifiConfig(&wifi_cfg);
    if(ret == ESP_OK){
        ESP_LOGI(TAG, "Đã tải cấu hình Wi-Fi từ NVS");
        ESP_LOGI(TAG, "SSID: %s", (char *)wifi_cfg.sta.ssid);
        ESP_LOGI(TAG, "Mật khẩu: %s", (char *)wifi_cfg.sta.password);
        wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_cfg.sta.pmf_cfg.capable = true;
        wifi_cfg.sta.pmf_cfg.required = false;
        // thiết lập mode 
        ret = esp_wifi_set_mode(WIFI_MODE_STA); 
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Thiết lập chế độ esp_wifi thất bại: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Thiết lập cấu hình esp_wifi thất bại: %s", esp_err_to_name(ret));
            return ret;
        }

        // khởi chạy wifi driver 
        ret = esp_wifi_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Khởi động esp_wifi thất bại: %s", esp_err_to_name(ret));
            return ret;
        }
        // kiểm tra đặt công suất 
        ret = esp_wifi_set_max_tx_power(40);
        if(ret != ESP_OK){
            ESP_LOGE(TAG, "Thiết lập công suất phát tối đa thất bại: %s", esp_err_to_name(ret));
            return ret; 
        }

    }else if(ret == ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGI(TAG, "NVS không chứa cấu hình wifi, cần vào chế độ kết nối ");
        // bật chế độ kết nối tự động, gửi lệnh bật đèn nháy màu xanh dương và set trạng thái thiết bị 
        set_current_door_mode(DEVICE_MODE_UNCONNECTED);

    }else{
        ESP_LOGE(TAG, "tải cấu hình WiFi thất bại: %s",esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

bool app_wifi_is_connected(void)
{
    if (s_wifi_event_group == NULL) {
        return false;
    }
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

bool app_wifi_wait_for_connect(uint32_t timeout_ms){
    if (s_wifi_event_group == NULL) {
        return false;
    }

    // Xóa các bit cũ trước khi chờ kết quả mới
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,                            // không tự clear bit
        pdFALSE,                            // chỉ cần 1 trong 2 bit
        pdMS_TO_TICKS(timeout_ms)           // chờ tối đa timeout_ms
    );

    return (bits & WIFI_CONNECTED_BIT) != 0;
}