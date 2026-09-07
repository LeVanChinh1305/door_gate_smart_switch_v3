#include "app_logic_mqtt_publisher.h"
#include "app_mqtt.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "mqtt_vconnex_decrypt.h"
#include "cJSON.h"
#include <sys/time.h>

static const char *TAG = "APP_MQTT_PUB";

esp_err_t app_logic_mqtt_publisher_SendResponse(const char *pcPayload)
{
    if (pcPayload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 1. Lấy con trỏ client quản lý kết nối MQTT */
    esp_mqtt_client_handle_t xClient = mqtt_app_GetClient();
    if (xClient == NULL) {
        ESP_LOGE(TAG, "Lỗi: MQTT Client chưa được khởi tạo hoặc mất kết nối");
        return ESP_FAIL;
    }

    /* 2. Lấy cấu hình thiết bị để trích xuất Topic Publish */
    const app_nvs_device_config_t *psConfig = mqtt_app_GetDeviceConfig();
    if (psConfig == NULL || psConfig->mqtt_pub[0] == '\0') {
        ESP_LOGE(TAG, "Lỗi: Topic Publish (mqtt_pub) chưa được cấu hình trong NVS");
        return ESP_FAIL;
    }

    /* 3. Đẩy bản tin với QoS 1 (Đảm bảo tới đích ít nhất 1 lần) */
    int iMsgId = esp_mqtt_client_publish(xClient, psConfig->mqtt_pub, pcPayload, 0, 1, 0);
    if (iMsgId < 0) {
        ESP_LOGE(TAG, "Đưa bản tin vào hàng đợi gửi thất bại");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Đã đẩy bản tin (MsgID: %d) lên Topic: %s", iMsgId, psConfig->mqtt_pub);
    return ESP_OK;
}

esp_err_t app_logic_mqtt_publisher_ReportGateData(uint8_t u8Gate1, uint8_t u8Gate2, uint8_t u8Gate3, uint8_t u8CurrentLevel)
{
    const app_nvs_device_config_t *psConfig = mqtt_app_GetDeviceConfig();
    if (psConfig == NULL || psConfig->mqtt_pub[0] == '\0') {
        return ESP_FAIL;
    }

    /* 1. Tạo chuỗi plaintext */
    char acPlaintext[256];
    snprintf(acPlaintext, sizeof(acPlaintext), 
             "[{\"param\":\"gate_1\",\"value\":%u},"
             "{\"param\":\"gate_2\",\"value\":%u},"
             "{\"param\":\"gate_3\",\"value\":%u},"
             "{\"param\":\"open_level\",\"value\":%u},"
             "{\"param\":\"gate_open_gap\",\"value\":0},"
             "{\"param\":\"sensor\",\"value\":1}]",
             u8Gate1, u8Gate2, u8Gate3, u8CurrentLevel);

    /* 2. Mã hóa chuỗi plaintext */
    uint8_t au8Ciphertext[DF_MQTT_CRYPTO_MAX_BUFFER_SIZE];
    size_t zCipherLen = 0;
    
    /* Gọi hàm mã hóa có sẵn trong mqtt_vconnex_decrypt.c */
    esp_err_t eErr = encrypt_vconnex_payload(acPlaintext, 
                                             strlen(acPlaintext), 
                                             psConfig->api_secret_key, 
                                             au8Ciphertext, 
                                             sizeof(au8Ciphertext), 
                                             &zCipherLen);
    
    if (eErr != ESP_OK) {
        ESP_LOGE(TAG, "Mã hóa bản tin ReportGateData thất bại");
        return eErr;
    }

    /* 3. Tạo JSON vỏ ngoài (CmdGetData) */
    cJSON *jsRoot = cJSON_CreateObject();
    cJSON_AddStringToObject(jsRoot, "name", "CmdGetData");
    cJSON_AddNumberToObject(jsRoot, "devT", psConfig->dev_type);
    cJSON_AddNumberToObject(jsRoot, "batteryPercent", 100);
    cJSON_AddStringToObject(jsRoot, "devExtAddr", psConfig->dev_ext_addr);
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t u64Timestamp = (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
    cJSON_AddNumberToObject(jsRoot, "timeStamp", (double)u64Timestamp);

    /* 4. Chuyển mảng mã hóa thành cJSON Array */
    cJSON *jsEncryptArray = cJSON_CreateArray();
    for (size_t i = 0; i < zCipherLen; i++) {
        cJSON_AddItemToArray(jsEncryptArray, cJSON_CreateNumber(au8Ciphertext[i]));
    }
    cJSON_AddItemToObject(jsRoot, "devVEncrypt", jsEncryptArray);

    /* 5. Xuất chuỗi và Publish */
    char *pcResponseJson = cJSON_PrintUnformatted(jsRoot);
    if (pcResponseJson != NULL) {
        (void)app_logic_mqtt_publisher_SendResponse(pcResponseJson);
        free(pcResponseJson);
    }
    cJSON_Delete(jsRoot);
    
    return ESP_OK;
}