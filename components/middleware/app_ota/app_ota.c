/**
 * @file app_ota.c
 * @brief Hỗ trợ nạp OTA linh hoạt cho cả HTTP Local và HTTPS Cloud
 */

#include "app_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_logic_mqtt_publisher.h"
#include <string.h>
#include "app_common.h"
#include "app_logic_ble_ibeacon.h"

static const char *TAG = "APP_OTA";

#define DF_OTA_URL_MAX_LEN          (256U)
#define DF_OTA_BUF_SIZE             (4096U)

static e_ota_state_t g_eOtaState = E_OTA_STATE_IDLE;

/* Cấp phát tĩnh buffer đệm ghi Flash để tránh phân mảnh Heap RAM (malloc failure) */
static char s_acOtaWriteBuf[DF_OTA_BUF_SIZE];

typedef struct {
    char acUrl[DF_OTA_URL_MAX_LEN];
} app_ota_param_t;

/* Tham số OTA tĩnh — tránh malloc/free: OTA chỉ chạy 1 tác vụ tại một thời điểm */
static app_ota_param_t s_sOtaParam;

e_ota_state_t app_ota_GetState(void)
{
    return g_eOtaState;
}

static void prv_OtaTask(void *pvParam)
{
    app_ota_param_t *psParam = (app_ota_param_t *)pvParam;
    g_eOtaState = E_OTA_STATE_DOWNLOADING;

    ESP_LOGI(TAG, "Bắt đầu tải và ghi OTA từ URL: %s", psParam->acUrl);

    /* Cấu hình HTTP Client tối ưu cho HTTP Local */
    esp_http_client_config_t sHttpConfig = {
        .url = psParam->acUrl,
        .transport_type = HTTP_TRANSPORT_OVER_TCP, /* Ép dùng Socket TCP thô, tránh kích hoạt SSL */
        .timeout_ms = 30000,                       /* Nâng timeout lên 30s xử lý trễ ghi Flash */
        .buffer_size = 2048,                       /* Bộ đệm Socket HTTP 2KB để tiết kiệm RAM */
        .buffer_size_tx = 1024,
        .keep_alive_enable = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&sHttpConfig);
    if (client == NULL) {
        ESP_LOGE(TAG, "Khởi tạo HTTP Client thất bại");
        g_eOtaState = E_OTA_STATE_FAILED;
        vTaskDelete(NULL);
        return;
    }

    esp_err_t eErr = esp_http_client_open(client, 0);
    if (eErr != ESP_OK) {
        ESP_LOGE(TAG, "Mở kết nối HTTP thất bại: %s", esp_err_to_name(eErr));
        esp_http_client_cleanup(client);
        g_eOtaState = E_OTA_STATE_FAILED;
        vTaskDelete(NULL);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP phản hồi: Status=%d, Content-Length=%d", status_code, content_length);

    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP Status không hợp lệ (%d != 200), hủy tiến trình OTA", status_code);
        esp_http_client_cleanup(client);
        g_eOtaState = E_OTA_STATE_FAILED;
        vTaskDelete(NULL);
        return;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Không tìm thấy phân vùng OTA tiếp theo");
        esp_http_client_cleanup(client);
        g_eOtaState = E_OTA_STATE_FAILED;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Đang ghi firmware vào phân vùng: %s tại Offset 0x%" PRIx32, 
             update_partition->label, update_partition->address);

    esp_ota_handle_t update_handle = 0;
    eErr = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (eErr != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin thất bại: %s", esp_err_to_name(eErr));
        esp_http_client_cleanup(client);
        g_eOtaState = E_OTA_STATE_FAILED;
        vTaskDelete(NULL);
        return;
    }

    int data_read = 0;
    int binary_file_len = 0;
    int last_log_len = 0;
    int retry_cnt = 0;
    bool bSuccess = true;

    while (1) {
        data_read = esp_http_client_read(client, s_acOtaWriteBuf, DF_OTA_BUF_SIZE);
        if (data_read < 0) {
            /* Nếu bị timeout/nghẽn tạm thời, cho phép thử lại tối đa 5 lần trước khi hủy */
            if (retry_cnt < 5) {
                retry_cnt++;
                ESP_LOGW(TAG, "HTTP stream read timeout, retrying (%d/5)...", retry_cnt);
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            ESP_LOGE(TAG, "Lỗi đọc dữ liệu từ HTTP Stream (đã đọc %d bytes)", binary_file_len);
            bSuccess = false;
            break;
        } else if (data_read == 0) {
            /* Tải hết file hoàn tất */
            break;
        }

        retry_cnt = 0; /* Reset bộ đếm retry khi đọc thành công dữ liệu mới */

        eErr = esp_ota_write(update_handle, (const void *)s_acOtaWriteBuf, data_read);
        if (eErr != ESP_OK) {
            ESP_LOGE(TAG, "Lỗi ghi dữ liệu vào Flash: %s", esp_err_to_name(eErr));
            bSuccess = false;
            break;
        }

        binary_file_len += data_read;
        if (binary_file_len - last_log_len >= (200 * 1024)) {
            last_log_len = binary_file_len;
            if (content_length > 0) {
                ESP_LOGI(TAG, "Tiến độ nạp OTA: %d / %d bytes (%.1f%%)",
                         binary_file_len, content_length,
                         ((float)binary_file_len / (float)content_length) * 100.0f);
            } else {
                ESP_LOGI(TAG, "Tiến độ nạp OTA: %d bytes...", binary_file_len);
            }
        }

        /* Nghỉ nhẹ 5ms sau mỗi block 4KB để nhường CPU cho WiFi Stack gửi gói TCP ACK */
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    esp_http_client_cleanup(client);

    if (bSuccess && (esp_ota_end(update_handle) == ESP_OK)) {
        eErr = esp_ota_set_boot_partition(update_partition);
        if (eErr == ESP_OK) {
            g_eOtaState = E_OTA_STATE_SUCCESS;
            ESP_LOGI(TAG, ">>> CẬP NHẬT OTA THÀNH CÔNG (%d bytes)! Tự khởi động lại sau 1.5s...", binary_file_len);
            vTaskDelay(pdMS_TO_TICKS(1500));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Lỗi thiết lập phân vùng Boot mới: %s", esp_err_to_name(eErr));
            g_eOtaState = E_OTA_STATE_FAILED;
        }
    } else {
        ESP_LOGE(TAG, "Tiến trình nạp OTA không hoàn tất");
        esp_ota_abort(update_handle);
        g_eOtaState = E_OTA_STATE_FAILED;
    }

    vTaskDelete(NULL);
}

esp_err_t app_ota_ProcessCmdStartOta(const cJSON *jsValue)
{
    if (jsValue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_eOtaState == E_OTA_STATE_DOWNLOADING) {
        ESP_LOGW(TAG, "OTA đang chạy, từ chối lệnh mới");
        return ESP_ERR_INVALID_STATE;
    }

    const cJSON *jsTarget = jsValue;
    const cJSON *jsValObj = cJSON_GetObjectItem(jsValue, "value");
    if (jsValObj != NULL) {
        jsTarget = jsValObj;
    }

    cJSON *jsModules = cJSON_GetObjectItem(jsTarget, "modules");
    if (jsModules == NULL) {
        ESP_LOGE(TAG, "Không tìm thấy 'modules'");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *jsWifi = cJSON_GetObjectItem(jsModules, "wifi");
    if (jsWifi == NULL) {
        ESP_LOGE(TAG, "Không tìm thấy 'wifi'");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *jsUrl = cJSON_GetObjectItem(jsWifi, "url");
    if ((jsUrl == NULL) || (!cJSON_IsString(jsUrl)) || (jsUrl->valuestring == NULL)) {
        ESP_LOGE(TAG, "Trường 'url' OTA không hợp lệ");
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(s_sOtaParam.acUrl, sizeof(s_sOtaParam.acUrl), "%s", jsUrl->valuestring);

    BaseType_t xRet = xTaskCreate(prv_OtaTask, "ota_task", DF_TASK_STACK_MEDIUM, (void *)&s_sOtaParam, DF_TASK_PRIO_MAX, NULL);
    if (xRet != pdPASS) {
        ESP_LOGE(TAG, "Tạo ota_task thất bại");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "OTA task đã tạo → dừng BLE iBeacon để giải phóng RAM");

    esp_err_t eBleRet = app_logic_ble_ibeacon_Deinit();
    if (eBleRet != ESP_OK) {
        ESP_LOGW(TAG, "Dừng BLE thất bại: %s", esp_err_to_name(eBleRet));
    }

    return ESP_OK;
}