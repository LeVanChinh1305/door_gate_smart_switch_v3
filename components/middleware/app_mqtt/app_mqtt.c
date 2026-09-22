/**
 * @file    app_mqtt.c
 * @brief   Triển khai dịch vụ MQTT Client, đăng ký sự kiện kết nối, subscription và điều phối bản tin thô.
 */

#include "app_mqtt.h"
#include "app_nvs.h"
#include "app_common.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "app_logic_mqtt.h"
#include "app_sntp.h"
#include "esp_heap_caps.h" 

static esp_mqtt_client_handle_t g_xMqttClient = NULL;
static bool g_bIsConnected = false;
static app_nvs_device_config_t g_sDeviceConfig = {0};

static const char *TAG = "APP_MQTT";

/**
 * @brief   Lấy con trỏ quản lý client MQTT hiện tại.
 * @return  esp_mqtt_client_handle_t Con trỏ đến handle của MQTT client
 */
esp_mqtt_client_handle_t app_mqtt_GetClient(void){
    return g_xMqttClient;
}

/**
 * @brief   Lấy thông tin cấu hình thiết bị đang được MQTT module lưu trữ.
 * @return  const app_nvs_device_config_t* Con trỏ hằng trỏ tới cấu trúc cấu hình thiết bị.
 */
const app_nvs_device_config_t *app_mqtt_GetDeviceConfig(void){
    return &g_sDeviceConfig;
}

/**
 * @brief   Hàm xử lý sự kiện MQTT nội bộ của ESP-MQTT client.
 * @param   handler_args Con trỏ tham số truyền vào callback.
 * @param   base Cơ sở sự kiện.
 * @param   event_id Mã định danh sự kiện MQTT.
 * @param   event_data Con trỏ chứa dữ liệu chi tiết của sự kiện.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    DF_UNUSED(handler_args);
    DF_UNUSED(base);
    
    esp_mqtt_event_handle_t pEvent = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT đã kết nối với broker");
        ESP_LOGI("HEAP", "MQTT Connected - Free heap: %u", (unsigned)esp_get_free_heap_size());
        g_bIsConnected = true;

        int i32MsgId = esp_mqtt_client_subscribe(g_xMqttClient, g_sDeviceConfig.mqtt_sub, 1);
        if (i32MsgId < 0) {
            ESP_LOGE(TAG, "Subscribe MQTT thất bại, msg_id=%d", i32MsgId);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT đã ngắt kết nối khỏi Broker");
        g_bIsConnected = false;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT Subscribed successfully, msg_id=%d", pEvent->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT Unsubscribed, msg_id=%d", pEvent->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "MQTT Published, msg_id=%d", pEvent->msg_id);
        break; 

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "=================== MQTT Data received ===================");
        ESP_LOGI(TAG, "topic: %.*s", pEvent->topic_len, pEvent->topic);
        ESP_LOGI(TAG, "Data: %.*s", pEvent->data_len, pEvent->data);
        (void)app_logic_mqtt_EnqueueData(pEvent->data, (uint32_t)pEvent->data_len);
        break; 

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT Event Error");
        ESP_LOGE("HEAP", "MQTT Error - Free heap: %u", (unsigned)esp_get_free_heap_size());
        ESP_LOGE("HEAP", "Largest block: %u",
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
        if (pEvent->error_handle != NULL) {
            ESP_LOGE(TAG, "MQTT error_type=%d tls_esp_err=0x%x sock_errno=%d (%s)",
                     pEvent->error_handle->error_type,
                     pEvent->error_handle->esp_tls_last_esp_err,
                     pEvent->error_handle->esp_transport_sock_errno,
                     strerror(pEvent->error_handle->esp_transport_sock_errno));
        }
        break;

    default:
        ESP_LOGD(TAG, "Other event id:%d", (int)event_id);
        break;
    }
}

/**
 * @brief   Khởi tạo và kích hoạt kết nối MQTT Client dựa trên cấu hình thiết bị.
 * @param   pDeviceConfig Con trỏ chứa cấu hình thiết bị tải từ NVS.
 * @return  esp_err_t ESP_OK nếu thành công, hoặc mã lỗi chuyên sâu ngược lại.
 */
esp_err_t app_mqtt_StartInit(app_nvs_device_config_t *pDeviceConfig){
    DF_CHECK_NULL_PARAM(pDeviceConfig); 
    if(pDeviceConfig->broker[0] == '\0'){
        ESP_LOGE(TAG, "Cấu hình MQTT broker không hợp lệ"); 
        return ESP_ERR_INVALID_ARG; 
    }
    if(g_xMqttClient != NULL){
        ESP_LOGE(TAG, "MQTT client đã được khởi tạo trước đó");
        return ESP_OK;
    }
    g_sDeviceConfig = *pDeviceConfig; 

    /* ===== LOG HEAP TRƯỚC KHI LÀM GÌ ===== */
    ESP_LOGI("HEAP", "Trước StartInit - Free heap: %u bytes", (unsigned)esp_get_free_heap_size());
    ESP_LOGI("HEAP", "Trước StartInit - Largest block: %u bytes",
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

    /* TẦNG 2: Chờ SNTP đồng bộ ngầm tối đa 8 giây */
    ESP_LOGI(TAG, "Đang chờ kiểm tra đồng bộ giờ SNTP Tầng 2...");
    if (!app_sntp_WaitForSync(8000U)) {
        time_t tNow = 0;
        (void)time(&tNow);

        /* KIỂM TRA ĐIỀU KIỆN MỀM: Nếu Tầng 1 đã gán giờ hợp lệ (> 2024), vẫn cho phép TLS hoạt động */
        if (tNow > 1700000000LL) {
            ESP_LOGW(TAG, "SNTP chưa xong nhưng giờ Tầng 1 hợp lệ (Timestamp: %lld). Tiếp tục TLS...", (long long)tNow);
        } else {
            ESP_LOGE(TAG, "Giờ hệ thống không hợp lệ (1970). Hủy kết nối TLS để tránh lỗi!");
            return ESP_ERR_TIMEOUT; 
        }
    } else {
        ESP_LOGI(TAG, "Đồng bộ thời gian SNTP Tầng 2 thành công!");
    }
    ESP_LOGI(TAG, "Đồng bộ thời gian thành công!");

    ESP_LOGI(TAG, "MQTT BROKER URI: %s", g_sDeviceConfig.broker);
    ESP_LOGI(TAG, "MQTT subscrible topic: %s", g_sDeviceConfig.mqtt_sub);
    ESP_LOGI(TAG, "MQTT publish topic: %s", g_sDeviceConfig.mqtt_pub);
    ESP_LOGI(TAG, "MQTT alert topic: %s", g_sDeviceConfig.mqtt_alert);

    /* ===== LOG HEAP TRƯỚC KHI TẠO CLIENT ===== */
    ESP_LOGI("HEAP", "Trước esp_mqtt_client_init - Free heap: %u", (unsigned)esp_get_free_heap_size());
    ESP_LOGI("HEAP", "Trước esp_mqtt_client_init - Largest block: %u",
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

    esp_mqtt_client_config_t sMqttCfg = {
        .broker.address.uri = g_sDeviceConfig.broker,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.username = g_sDeviceConfig.username,
        .credentials.authentication.password = g_sDeviceConfig.password,
        .task.stack_size = DF_TASK_STACK_MAX, 
        .task.priority = DF_TASK_PRIO_NORMAL,   
        .buffer.size = 2048,
        .buffer.out_size = 2048,
        .outbox.limit = 1024 * 4, // giới hạn bộ đệm Outbox tối đa 4KB 
        .network.timeout_ms = 20000,
    };

    g_xMqttClient = esp_mqtt_client_init(&sMqttCfg); 
    if(g_xMqttClient == NULL){
        ESP_LOGE(TAG, "khởi tạo MQTT client thất bại");
        return ESP_FAIL;
    }

    /* ===== LOG HEAP SAU KHI TẠO CLIENT ===== */
    ESP_LOGI("HEAP", "Sau esp_mqtt_client_init - Free heap: %u", (unsigned)esp_get_free_heap_size());
    ESP_LOGI("HEAP", "Sau esp_mqtt_client_init - Largest block: %u",
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

    esp_err_t eRet = esp_mqtt_client_register_event(g_xMqttClient, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (eRet != ESP_OK) {
        ESP_LOGE(TAG, "Đăng ký MQTT event handler thất bại: %s", esp_err_to_name(eRet));
        return eRet;
    }
    
    eRet = esp_mqtt_client_start(g_xMqttClient);
    if (eRet != ESP_OK) {
        ESP_LOGE(TAG, "Khởi động MQTT Client thất bại: %s", esp_err_to_name(eRet));
        return eRet;
    }
    ESP_LOGI("HEAP", "Sau esp_mqtt_client_start - Free heap: %u", (unsigned)esp_get_free_heap_size());
    return ESP_OK;
}

/**
 * @brief   Kiểm tra trạng thái kết nối mạng MQTT hiện tại.
 * @return  true nếu đã kết nối thành công, ngược lại false.
 */
bool app_mqtt_IsConnected(void) { 
    return g_bIsConnected; 
}


/**
 * @brief   Dừng và giải phóng tài nguyên MQTT Client cùng TLS Buffer về cho Heap.
 * @return  ESP_OK nếu xử lý thành công.
 */
esp_err_t app_mqtt_Stop(void)
{
    if (g_xMqttClient != NULL) {
        ESP_LOGI(TAG, "Đang dừng MQTT Client để giải phóng RAM cho chế độ kết nối...");
        
        /* 1. Dừng Task Client và đóng Socket TLS */
        (void)esp_mqtt_client_stop(g_xMqttClient);
        
        /* 2. Hủy Handle và giải phóng Dynamic Heap Memory */
        (void)esp_mqtt_client_destroy(g_xMqttClient);
        
        /* 3. Reset con trỏ và cờ trạng thái */
        g_xMqttClient = NULL;
        g_bIsConnected = false;
        
        ESP_LOGI(TAG, "Đã hủy MQTT Client và thu hồi RAM thành công!");
    }
    return ESP_OK;
}