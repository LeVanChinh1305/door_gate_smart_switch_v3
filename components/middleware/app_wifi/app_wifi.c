/**
 * @file app_wifi.c
 * @brief Triển khai khởi tạo Wi-Fi STA và theo dõi trạng thái kết nối.
 */

#include "app_wifi.h"
#include "app_nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "stdbool.h"
#include "esp_log.h"
#include "nvs.h"
#include "app_blufi.h"
#include "app_device_state.h"
#include "app_sntp.h"

static const char *TAG = "APP_WIFI";
static int32_t g_i32WifiRetryCount = 0;
static EventGroupHandle_t g_hWifiEventGroup = NULL; 

/**
 * @brief Xử lý sự kiện Wi-Fi và IP, bao gồm kết nối lại khi bị ngắt.
 * @param pArg Tham số sự kiện, hiện không sử dụng.
 * @param eEventBase Nhóm sự kiện Wi-Fi hoặc IP.
 * @param i32EventId Mã sự kiện cần xử lý.
 * @param pEventData Dữ liệu đi kèm sự kiện, có thể là NULL.
 * @return Không trả về.
 */
static void wifi_event_handler(void *pArg, esp_event_base_t eEventBase, int32_t i32EventId, void *pEventData)
{
    if (eEventBase == WIFI_EVENT) {
        switch (i32EventId) {
            case WIFI_EVENT_STA_START:
                if (app_nvs_IsProvisionedWifiConfig()) {
                    ESP_LOGI(TAG, "Wi-Fi STA đã khởi động, đang kết nối...");
                    (void)esp_wifi_connect();
                } else {
                    ESP_LOGI(TAG, "Wi-Fi STA đã khởi động, đang chờ cấu hình BLE...");
                }
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Wi-Fi đã kết nối đến AP, đang chờ địa chỉ IP");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
            {
                wifi_event_sta_disconnected_t *pEvent = (wifi_event_sta_disconnected_t *)pEventData;
                if (pEvent != NULL) {
                    ESP_LOGW(TAG, "Đã ngắt kết nối khỏi AP, mã nguyên nhân: %d", pEvent->reason);
                } else {
                    ESP_LOGW(TAG, "Đã ngắt kết nối khỏi AP, không có thông tin nguyên nhân");
                }

                if (g_i32WifiRetryCount < DF_WIFI_MAX_RETRY_COUNT) {
                    g_i32WifiRetryCount++;
                    ESP_LOGI(TAG, "Đang thử kết nối lại %ld/%d", (long)g_i32WifiRetryCount, DF_WIFI_MAX_RETRY_COUNT);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    (void)esp_wifi_connect();
                } else {
                    g_i32WifiRetryCount = 0;
                    if (g_hWifiEventGroup != NULL) {
                        (void)xEventGroupSetBits(g_hWifiEventGroup, DF_WIFI_FAIL_BIT);
                    }
                    ESP_LOGE(TAG, "Không thể kết nối Wi-Fi sau %d lần thử", DF_WIFI_MAX_RETRY_COUNT);
                }
                break;
            }
            default:
                break; 
        }
    } else if (eEventBase == IP_EVENT) {
        switch (i32EventId) {
            case IP_EVENT_STA_GOT_IP:
            {
                ip_event_got_ip_t *pEvent = (ip_event_got_ip_t *)pEventData;
                if (pEvent != NULL) {
                    ESP_LOGI(TAG, "Đã nhận địa chỉ IP: " IPSTR, IP2STR(&pEvent->ip_info.ip));
                }
                g_i32WifiRetryCount = 0;
                if (g_hWifiEventGroup != NULL) {
                    (void)xEventGroupSetBits(g_hWifiEventGroup, DF_WIFI_CONNECTED_BIT);
                }
                if (app_blufi_IsConnected()) {
                    app_blufi_ReportWifiStatus(true);
                }
                ESP_LOGI(TAG, "tiến hành đồng bộ SNTP...");
                app_sntp_Init();
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

/**
 * @brief Khởi tạo các thành phần mạng và bắt đầu Wi-Fi STA nếu có cấu hình.
 * @param None.
 * @return ESP_OK nếu thành công; mã lỗi của thành phần thất bại.
 */
esp_err_t app_wifi_InitSta(void)
{
    if (g_hWifiEventGroup == NULL) {
        g_hWifiEventGroup = xEventGroupCreate(); 
        if (g_hWifiEventGroup == NULL) {
            ESP_LOGE(TAG, "Khởi tạo event group thất bại");
            return ESP_ERR_NO_MEM; 
        }
    }
    
    (void)xEventGroupClearBits(g_hWifiEventGroup, DF_WIFI_CONNECTED_BIT | DF_WIFI_FAIL_BIT); 
    
    esp_err_t eRet = esp_netif_init();  
    if (eRet != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo esp_netif thất bại: %s", esp_err_to_name(eRet));
        return eRet; 
    }

    eRet = esp_event_loop_create_default();  
    if (eRet != ESP_OK && eRet != ESP_ERR_INVALID_STATE) {  
        ESP_LOGE(TAG, "Tạo event loop mặc định thất bại: %s", esp_err_to_name(eRet));
        return eRet;
    }

    esp_netif_t *pStaNetif = esp_netif_create_default_wifi_sta(); 
    if (pStaNetif == NULL) {
        ESP_LOGE(TAG, "Tạo Wi-Fi STA mặc định thất bại");
        return ESP_FAIL;
    }

    wifi_init_config_t sCfg = WIFI_INIT_CONFIG_DEFAULT();
    eRet = esp_wifi_init(&sCfg);
    if (eRet != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo esp_wifi thất bại: %s", esp_err_to_name(eRet));
        return eRet; 
    }

    eRet = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL); 
    if (eRet != ESP_OK) {
        ESP_LOGE(TAG, "Đăng ký sự kiện Wi-Fi thất bại: %s", esp_err_to_name(eRet));
        return eRet; 
    }
    
    eRet = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL); 
    if (eRet != ESP_OK) {
        ESP_LOGE(TAG, "Đăng ký sự kiện IP thất bại: %s", esp_err_to_name(eRet));
        return eRet; 
    }

    wifi_config_t sWifiCfg = {0}; 
    eRet = app_nvs_LoadWifiConfig(&sWifiCfg);
    if (eRet == ESP_OK) {
        ESP_LOGI(TAG, "Đã tải cấu hình Wi-Fi từ NVS");
        ESP_LOGI(TAG, "SSID: %s", (char *)sWifiCfg.sta.ssid);
        
        sWifiCfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sWifiCfg.sta.pmf_cfg.capable = true;
        sWifiCfg.sta.pmf_cfg.required = false;
        
        eRet = esp_wifi_set_mode(WIFI_MODE_STA); 
        if (eRet != ESP_OK) {
            ESP_LOGE(TAG, "Thiết lập chế độ esp_wifi thất bại: %s", esp_err_to_name(eRet));
            return eRet;
        }

        eRet = esp_wifi_set_config(WIFI_IF_STA, &sWifiCfg);
        if (eRet != ESP_OK) {
            ESP_LOGE(TAG, "Thiết lập cấu hình esp_wifi thất bại: %s", esp_err_to_name(eRet));
            return eRet;
        }

        eRet = esp_wifi_start();
        if (eRet != ESP_OK) {
            ESP_LOGE(TAG, "Khởi động esp_wifi thất bại: %s", esp_err_to_name(eRet));
            return eRet;
        }
        
        eRet = esp_wifi_set_max_tx_power(DF_WIFI_MAX_TX_POWER);
        if (eRet != ESP_OK) {
            ESP_LOGE(TAG, "Thiết lập công suất phát tối đa thất bại: %s", esp_err_to_name(eRet));
            return eRet; 
        }

    } else if (eRet == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "NVS không chứa cấu hình wifi, chuyển sang chế độ chờ kết nối");
        set_current_door_mode(DEVICE_MODE_UNCONNECTED);
    } else {
        ESP_LOGE(TAG, "Tải cấu hình WiFi thất bại: %s", esp_err_to_name(eRet));
        return eRet;
    }

    return ESP_OK;
}

/**
 * @brief Đọc cờ trạng thái Wi-Fi đã kết nối.
 * @param None.
 * @return true nếu đã kết nối; false nếu chưa khởi tạo hoặc chưa kết nối.
 */
bool app_wifi_IsConnected(void)
{
    if (g_hWifiEventGroup == NULL) {
        return false;
    }
    EventBits_t uxBits = xEventGroupGetBits(g_hWifiEventGroup);
    return (uxBits & DF_WIFI_CONNECTED_BIT) != 0;
}

/**
 * @brief Chờ một trong các sự kiện kết nối hoặc thất bại.
 * @param u32TimeoutMs Thời gian chờ tối đa, tính bằng mili-giây.
 * @return true nếu nhận được sự kiện kết nối; false nếu thất bại hoặc hết thời gian.
 */
bool app_wifi_WaitForConnect(uint32_t u32TimeoutMs)
{
    if (g_hWifiEventGroup == NULL) {
        return false;
    }

    (void)xEventGroupClearBits(g_hWifiEventGroup, DF_WIFI_CONNECTED_BIT | DF_WIFI_FAIL_BIT);

    EventBits_t uxBits = xEventGroupWaitBits(
        g_hWifiEventGroup,
        DF_WIFI_CONNECTED_BIT | DF_WIFI_FAIL_BIT,
        pdFALSE,                            
        pdFALSE,                            
        pdMS_TO_TICKS(u32TimeoutMs)           
    );

    return (uxBits & DF_WIFI_CONNECTED_BIT) != 0;
}