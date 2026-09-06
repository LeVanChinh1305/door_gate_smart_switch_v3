/**
 * @file    app_logic_mqtt.c
 * @brief   Triển khai logic nghiệp vụ ứng dụng cho MQTT: Hàng đợi, phân loại lệnh và giải mã payload.
 */

#include "app_logic_mqtt.h"
#include "app_mqtt.h"
#include "app_common.h"
#include "mqtt_vconnex_decrypt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "APP_LOGIC_MQTT";

/* Biến toàn cục tĩnh quản lý hàng đợi nội bộ module */
static QueueHandle_t g_xMqttQueue = NULL;

/**
 * @brief   Giải mã và xử lý bản tin CmdSetData có chứa trường mã hóa devVEncrypt.
 * @param   pValue Con trỏ cJSON trỏ tới đối tượng "value" trong gói tin JSON.
 */
static void app_logic_mqtt_HandleSetData(const cJSON *pValue)
{
    if (pValue == NULL) {
        return;
    }

    const cJSON *jsEncryptArray = cJSON_GetObjectItem(pValue, "devVEncrypt");
    if (!cJSON_IsArray(jsEncryptArray)) {
        ESP_LOGW(TAG, "Không tìm thấy mảng devVEncrypt hợp lệ trong CmdSetData");
        return;
    }

    int iCipherLen = cJSON_GetArraySize(jsEncryptArray);
    if (iCipherLen <= 0 || (iCipherLen % 16) != 0 || iCipherLen >= (int)DF_MQTT_CRYPTO_MAX_BUFFER_SIZE) {
        ESP_LOGE(TAG, "Độ dài ciphertext không hợp lệ hoặc vượt quá giới hạn đệm tĩnh: %d", iCipherLen);
        return;
    }

    /* 1. Cấp phát tạm bộ đệm ciphertext từ mảng JSON */
    uint8_t *pu8Ciphertext = (uint8_t *)malloc((size_t)iCipherLen);
    if (pu8Ciphertext == NULL) {
        ESP_LOGE(TAG, "Không đủ bộ nhớ Heap để cấp phát ciphertext");
        return;
    }

    for (int i = 0; i < iCipherLen; i++) {
        const cJSON *jsItem = cJSON_GetArrayItem(jsEncryptArray, i);
        if (cJSON_IsNumber(jsItem)) {
            pu8Ciphertext[i] = (uint8_t)jsItem->valueint;
        } else {
            pu8Ciphertext[i] = 0U;
        }
    }

    /* 2. Lấy cấu hình thiết bị để trích xuất khóa bí mật api_secret_key */
    const app_nvs_device_config_t *psConfig = mqtt_app_GetDeviceConfig();
    if (psConfig == NULL || strlen(psConfig->api_secret_key) < 32U) {
        ESP_LOGE(TAG, "API Secret Key không hợp lệ hoặc chưa được cấu hình trong NVS");
        free(pu8Ciphertext);
        return;
    }

    /* 3. Sử dụng bộ đệm tĩnh (char array) đầu ra theo chuẩn hàm giải mã mới */
    char acPlaintextBuffer[DF_MQTT_CRYPTO_MAX_BUFFER_SIZE];
    size_t zPlaintextLen = 0U;

    (void)memset(acPlaintextBuffer, 0, sizeof(acPlaintextBuffer));

    esp_err_t eErr = decrypt_vconnex_payload(pu8Ciphertext, 
                                            (size_t)iCipherLen,
                                            psConfig->api_secret_key,
                                            acPlaintextBuffer,
                                            sizeof(acPlaintextBuffer),
                                            &zPlaintextLen);
    
    /* Giải phóng ciphertext ngay sau khi giải mã xong */
    free(pu8Ciphertext);

    if (eErr == ESP_OK && zPlaintextLen > 0U) {
        ESP_LOGI(TAG, "Giải mã payload thành công, nội dung: %s", acPlaintextBuffer);

        /* 4. Phân tích nội dung JSON bên trong chuỗi plaintext sau giải mã */
        cJSON *jsInnerJson = cJSON_Parse(acPlaintextBuffer);
        if (jsInnerJson != NULL) {
            /* TODO: Triển khai các kịch bản điều khiển thiết bị thực tế (Relay, LED, v.v.) tại đây */
            cJSON_Delete(jsInnerJson);
        }
    } else {
        ESP_LOGE(TAG, "Giải mã vconnex payload thất bại, mã lỗi: %d", (int)eErr);
    }
}

/**
 * @brief   Task nền chuyên trách nhận bản tin từ Queue, phân loại lệnh và điều phối xử lý.
 * @param   pArg Tham số truyền vào task (không sử dụng).
 */
static void app_logic_mqtt_Task(void *pArg)
{
    DF_UNUSED(pArg);
    app_logic_mqtt_queue_item_t sItem;

    while (true) {
        if (xQueueReceive(g_xMqttQueue, &sItem, portMAX_DELAY) == pdPASS) {
            if (sItem.pcData != NULL && sItem.u32DataLen > 0U) {
                /* Parse chuỗi JSON payload nhận được từ hàng đợi */
                cJSON *jsRoot = cJSON_Parse(sItem.pcData);
                if (jsRoot != NULL) {
                    const cJSON *jsName = cJSON_GetObjectItem(jsRoot, "name");
                    const cJSON *jsValue = cJSON_GetObjectItem(jsRoot, "value");

                    if (cJSON_IsString(jsName) && jsName->valuestring != NULL) {
                        const char *pcCmdName = jsName->valuestring;
                        
                        if (strcmp(pcCmdName, "CmdGetData") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh CmdGetData: tiến hành đọc thông tin thiết bị và phản hồi");
                            
                        } 
                        else if (strcmp(pcCmdName, "CmdGetWifiInfo") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh CmdGetWifiInfo");
                        } 
                        else if (strcmp(pcCmdName, "CmdGetExtraConfig") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh CmdGetExtraConfig");
                        }
                        else if(strcmp(pcCmdName, "CmdGetDeviceInfo") ==0){
                            ESP_LOGI(TAG, "-> khớp lệnh CmdGetDeviceInfo");
                        }
                        else if(strcmp(pcCmdName, "CmdGetSensorConfig") ==0){
                            ESP_LOGI(TAG, "-> khớp lệnh CmdGetSensorConfig");
                        }
                        else if (strcmp(pcCmdName, "CmdSetData") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh CmdSetData, tiến hành gọi hàm giải mã...");
                            app_logic_mqtt_HandleSetData(jsValue);
                        } 
                        else {
                            ESP_LOGW(TAG, "-> Lệnh MQTT chưa được định nghĩa: %s", pcCmdName);
                        }
                    } else {
                        ESP_LOGW(TAG, "Không tìm thấy trường 'name' hoặc 'name' không phải là chuỗi trong JSON");
                    }
                    cJSON_Delete(jsRoot);
                } else {
                    ESP_LOGE(TAG, "cJSON_Parse thất bại với chuỗi payload: %s", sItem.pcData);
                }

                free(sItem.pcData);
                sItem.pcData = NULL;
            }
        }
    }
}
/**
 * @brief   Khởi tạo hàng đợi và luồng task nền ứng dụng MQTT.
 */
esp_err_t app_logic_mqtt_Init(void)
{
    g_xMqttQueue = xQueueCreate(DF_APP_LOGIC_MQTT_QUEUE_LEN, sizeof(app_logic_mqtt_queue_item_t));
    if (g_xMqttQueue == NULL) {
        ESP_LOGE(TAG, "Tạo hàng đợi MQTT Queue thất bại do thiếu bộ nhớ");
        return ESP_ERR_NO_MEM;
    }
    
    BaseType_t xRet = xTaskCreate(app_logic_mqtt_Task, 
                                  "mqtt_logic_task", 
                                  DF_TASK_STACK_NETWORK, 
                                  NULL, 
                                  DF_TASK_PRIO_NORMAL, 
                                  NULL);
    if (xRet != pdPASS) {
        ESP_LOGE(TAG, "Tạo task xử lý logic MQTT thất bại");
        vQueueDelete(g_xMqttQueue);
        g_xMqttQueue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Khởi tạo module app_logic_mqtt thành công");
    return ESP_OK;
}

/**
 * @brief   Đưa dữ liệu bản tin thô vào hàng đợi an toàn không gây nghẽn ngắt mạng.
 */
esp_err_t app_logic_mqtt_EnqueueData(const char *pcData, uint32_t u32DataLen)
{
    if (g_xMqttQueue == NULL || pcData == NULL || u32DataLen == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    app_logic_mqtt_queue_item_t sItem;
    sItem.u32DataLen = u32DataLen;
    sItem.pcData = (char *)malloc((size_t)u32DataLen + 1U);
    if (sItem.pcData == NULL) {
        return ESP_ERR_NO_MEM;
    }

    (void)memcpy(sItem.pcData, pcData, (size_t)u32DataLen);
    sItem.pcData[u32DataLen] = '\0';

    /* Đưa vào Queue với timeout = 0 để tránh block luồng mạng lõi */
    if (xQueueSend(g_xMqttQueue, &sItem, 0U) != pdPASS) {
        free(sItem.pcData);
        sItem.pcData = NULL;
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}