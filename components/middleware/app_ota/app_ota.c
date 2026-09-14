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
#include <stdlib.h>

static const char *TAG = "APP_OTA";

#define DF_OTA_URL_MAX_LEN          (256U)
#define DF_OTA_BUF_SIZE             (1024U)
#define DF_OTA_TASK_STACK_SIZE      (8192U)

static e_ota_state_t g_eOtaState = E_OTA_STATE_IDLE;

typedef struct {
    char acUrl[DF_OTA_URL_MAX_LEN];
} app_ota_param_t;

e_ota_state_t app_ota_GetState(void)
{
    return g_eOtaState;
}

static void prv_OtaTask(void *pvParam)
{
    app_ota_param_t *psParam = (app_ota_param_t *)pvParam;
    g_eOtaState = E_OTA_STATE_DOWNLOADING;

    ESP_LOGI(TAG, "Bắt đầu tải và ghi OTA từ URL: %s", psParam->acUrl);

    esp_http_client_config_t sHttpConfig = {
        .url = psParam->acUrl,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&sHttpConfig);
    if (client == NULL) {
        ESP_LOGE(TAG, "Khởi tạo HTTP Client thất bại");
        g_eOtaState = E_OTA_STATE_FAILED;
        free(psParam);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t eErr = esp_http_client_open(client, 0);
    if (eErr != ESP_OK) {
        ESP_LOGE(TAG, "Mở kết nối HTTP thất bại: %s", esp_err_to_name(eErr));
        esp_http_client_cleanup(client);
        g_eOtaState = E_OTA_STATE_FAILED;
        free(psParam);
        vTaskDelete(NULL);
        return;
    }

    (void)esp_http_client_fetch_headers(client);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Không tìm thấy phân vùng OTA tiếp theo");
        esp_http_client_cleanup(client);
        g_eOtaState = E_OTA_STATE_FAILED;
        free(psParam);
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
        free(psParam);
        vTaskDelete(NULL);
        return;
    }

    char *ota_write_buf = (char *)malloc(DF_OTA_BUF_SIZE);
    if (ota_write_buf == NULL) {
        ESP_LOGE(TAG, "Không đủ RAM cấp phát ota_write_buf");
        esp_ota_end(update_handle);
        esp_http_client_cleanup(client);
        g_eOtaState = E_OTA_STATE_FAILED;
        free(psParam);
        vTaskDelete(NULL);
        return;
    }

    int data_read = 0;
    int binary_file_len = 0;
    bool bSuccess = true;

    while (1) {
        data_read = esp_http_client_read(client, ota_write_buf, DF_OTA_BUF_SIZE);
        if (data_read < 0) {
            ESP_LOGE(TAG, "Lỗi đọc dữ liệu từ HTTP Stream");
            bSuccess = false;
            break;
        } else if (data_read == 0) {
            /* Tải hết file */
            break;
        }

        eErr = esp_ota_write(update_handle, (const void *)ota_write_buf, data_read);
        if (eErr != ESP_OK) {
            ESP_LOGE(TAG, "Lỗi ghi dữ liệu vào Flash: %s", esp_err_to_name(eErr));
            bSuccess = false;
            break;
        }

        binary_file_len += data_read;
    }

    free(ota_write_buf);
    esp_http_client_cleanup(client);

    if (bSuccess && (esp_ota_end(update_handle) == ESP_OK)) {
        eErr = esp_ota_set_boot_partition(update_partition);
        if (eErr == ESP_OK) {
            g_eOtaState = E_OTA_STATE_SUCCESS;
            ESP_LOGI(TAG, ">>> CẬP NHẬT OTA THÀNH CÔNG (%d bytes)! Tự khởi động lại sau 1.5s...", binary_file_len);
            vTaskDelay(pdMS_TO_TICKS(1500));
            free(psParam);
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

    free(psParam);
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

    app_ota_param_t *psOtaParam = (app_ota_param_t *)malloc(sizeof(app_ota_param_t));
    if (psOtaParam == NULL) {
        ESP_LOGE(TAG, "Không đủ RAM cấp phát psOtaParam");
        return ESP_ERR_NO_MEM;
    }

    snprintf(psOtaParam->acUrl, sizeof(psOtaParam->acUrl), "%s", jsUrl->valuestring);

    BaseType_t xRet = xTaskCreate(prv_OtaTask, "ota_task", DF_OTA_TASK_STACK_SIZE, (void *)psOtaParam, 5, NULL);
    if (xRet != pdPASS) {
        ESP_LOGE(TAG, "Tạo ota_task thất bại");
        free(psOtaParam);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}