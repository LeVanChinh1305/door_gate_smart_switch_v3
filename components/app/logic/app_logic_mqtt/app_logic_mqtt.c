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
#include "app_logic_relay.h"
#include "app_led_state.h"
#include "app_logic_mqtt_publisher.h" 
#include "app_logic_extra_config.h"
#include <time.h>

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

        /* 4. Phân tích nội dung JSON bên trong chuỗi plaintext sau giải mã (Chỉ parse 1 lần duy nhất) */
        cJSON *jsInnerJson = cJSON_Parse(acPlaintextBuffer);
        if (jsInnerJson != NULL) {
            const cJSON *jsParam = cJSON_GetObjectItem(jsInnerJson, "param");
            const cJSON *jsValue = cJSON_GetObjectItem(jsInnerJson, "value");

            if (cJSON_IsString(jsParam) && jsParam->valuestring != NULL) {
                const char *pcParam = jsParam->valuestring;
                int iValue = cJSON_IsNumber(jsValue) ? jsValue->valueint : 0;

                ESP_LOGI(TAG, "Điều khiển thiết bị - Param: %s, Value: %d", pcParam, iValue);

                /* Ánh xạ tham số từ app/cloud xuống lệnh điều khiển relay thực tế */
                if (strcmp(pcParam, "gate_3") == 0 || strcmp(pcParam, "up") == 0) {
                    if (iValue == 1) {
                        (void)app_logic_relay_Open();
                        ESP_LOGI(TAG, "-> Thực thi lệnh: Mở cửa (UP)");
                        /* Tắt toàn bộ và chỉ bật LED số 0 (Xanh lá) */
                        (void)app_led_state_SetState(E_LED_STATE_GATE_UP);
                        ESP_LOGI(TAG, "-> Đã set màu led"); 
                    }
                } 
                else if (strcmp(pcParam, "gate_1") == 0 || strcmp(pcParam, "down") == 0) {
                    if (iValue == 1) {
                        (void)app_logic_relay_Close();
                        ESP_LOGI(TAG, "-> Thực thi lệnh: Đóng cửa (DOWN)");
                        /* Tắt toàn bộ và chỉ bật LED số 1 xanh */
                        (void)app_led_state_SetState(E_LED_STATE_GATE_DOWN);
                        ESP_LOGI(TAG, "-> Đã set màu led"); 
                    }
                } 
                else if (strcmp(pcParam, "gate_2") == 0 || strcmp(pcParam, "stop") == 0) {
                    if (iValue == 1) {
                        (void)app_logic_relay_Stop();
                        ESP_LOGI(TAG, "-> Thực thi lệnh: Dừng cửa (STOP)");
                        /* Tắt toàn bộ và chỉ bật LED số 2 xanh*/
                        (void)app_led_state_SetState(E_LED_STATE_GATE_STOP);
                        ESP_LOGI(TAG, "-> Đã set màu led"); 
                    }
                }else if (strcmp(pcParam, "gate_level") == 0) {
                    /* Kiểm tra giới hạn an toàn của giá trị % (0 đến 100) */
                    if (iValue >= 0 && iValue <= 100) {
                        ESP_LOGI(TAG, "-> Thực thi lệnh: Điều khiển cửa đến mức %d%%", iValue);
                        
                        uint8_t u8TargetVal = (uint8_t)iValue;
                        uint8_t u8CurrentVal = app_logic_relay_GetCurrentLevel();
                        
                        ESP_LOGI(TAG, "-> Thực thi lệnh: Điều khiển cửa đến mức %u%% (Hiện tại: %u%%)", u8TargetVal, u8CurrentVal);
                        
                        if (u8TargetVal > u8CurrentVal) {
                            (void)app_led_state_SetState(E_LED_STATE_GATE_UP);
                        } 
                        else if (u8TargetVal < u8CurrentVal) {
                            (void)app_led_state_SetState(E_LED_STATE_GATE_DOWN);
                        } 
                        else {
                            (void)app_led_state_SetState(E_LED_STATE_GATE_STOP);
                        }

                        app_logic_relay_SetLevel(u8TargetVal);
 
                    } else {
                        ESP_LOGW(TAG, "Giá trị gate_level không hợp lệ (ngoài dải 0-100): %d", iValue);
                    }
                }else {
                    ESP_LOGW(TAG, "Param điều khiển không được hỗ trợ: %s", pcParam);
                }
            } else {
                ESP_LOGW(TAG, "Cấu trúc param hoặc value bên trong plaintext không hợp lệ");
            }

            cJSON_Delete(jsInnerJson);
        }
    } else {
        ESP_LOGE(TAG, "Giải mã vconnex payload thất bại, mã lỗi: %d", (int)eErr);
    }
}

/**
 * @brief   Giải mã và xử lý bản tin CmdAddSchedule (Hẹn giờ)
 * @param   jsRoot Con trỏ cJSON trỏ tới gốc của gói tin JSON.
 */
/**
 * @brief   Giải mã và xử lý bản tin CmdAddSchedule (Hẹn giờ)
 */
static void app_logic_mqtt_HandleAddAndUpdateSchedule(cJSON **ppjsRoot, const char *pcCmdName)
{
    if (ppjsRoot == NULL || *ppjsRoot == NULL) {
        return;
    }

    /* 0. TẠO BẢN SAO CHUỖI TÊN LỆNH TRÊN STACK (Tránh bị xoá mất khi cJSON_Delete) */
    char acCmdNameCopy[32];
    snprintf(acCmdNameCopy, sizeof(acCmdNameCopy), "%s", pcCmdName);
    
    cJSON *jsRoot = *ppjsRoot;

    /* 1. Tìm mảng valueEncrypt */
    const cJSON *jsEncryptArray = cJSON_GetObjectItem(jsRoot, "valueEncrypt");
    if (!cJSON_IsArray(jsEncryptArray)) {
        const cJSON *jsValue = cJSON_GetObjectItem(jsRoot, "value");
        if (jsValue != NULL) {
            jsEncryptArray = cJSON_GetObjectItem(jsValue, "valueEncrypt");
        }
    }

    if (!cJSON_IsArray(jsEncryptArray)) {
        ESP_LOGW(TAG, "Không tìm thấy mảng valueEncrypt trong CmdAddSchedule");
        return;
    }

    int iCipherLen = cJSON_GetArraySize(jsEncryptArray);
    if (iCipherLen <= 0 || (iCipherLen % 16) != 0 || iCipherLen >= (int)DF_MQTT_CRYPTO_MAX_BUFFER_SIZE) {
        ESP_LOGE(TAG, "Độ dài ciphertext hẹn giờ không hợp lệ: %d", iCipherLen);
        return;
    }

    /* 2. Ép kiểu dữ liệu mảng JSON sang mảng byte trên Stack */
    uint8_t au8Ciphertext[DF_MQTT_CRYPTO_MAX_BUFFER_SIZE];
    (void)memset(au8Ciphertext, 0, sizeof(au8Ciphertext));

    for (int i = 0; i < iCipherLen; i++) {
        const cJSON *jsItem = cJSON_GetArrayItem(jsEncryptArray, i);
        au8Ciphertext[i] = cJSON_IsNumber(jsItem) ? (uint8_t)jsItem->valueint : 0U;
    }

    /* 3. Lấy API Secret Key để giải mã */
    const app_nvs_device_config_t *psConfig = mqtt_app_GetDeviceConfig();
    if (psConfig == NULL || strlen(psConfig->api_secret_key) < 32U) {
        ESP_LOGE(TAG, "Lỗi API Secret Key không hợp lệ");
        return;
    }

    char acPlaintextBuffer[DF_MQTT_CRYPTO_MAX_BUFFER_SIZE];
    size_t zPlaintextLen = 0U;
    (void)memset(acPlaintextBuffer, 0, sizeof(acPlaintextBuffer));

    /* ====================================================================
     * QUAN TRỌNG: DỌN DẸP RAM TRƯỚC KHI GIẢI MÃ
     * Xóa toàn bộ cây JSON để gộp lại RAM trống cho Hardware AES
     * ==================================================================== */
    cJSON_Delete(*ppjsRoot);
    *ppjsRoot = NULL; /* Gán NULL để vòng lặp Task bên ngoài không xóa đúp gây Crash */

    /* 4. Gọi API giải mã AES-256-CBC */
    esp_err_t eErr = decrypt_vconnex_payload(au8Ciphertext, (size_t)iCipherLen,
                                             psConfig->api_secret_key,
                                             acPlaintextBuffer, sizeof(acPlaintextBuffer),
                                             &zPlaintextLen);

    if (eErr == ESP_OK && zPlaintextLen > 0U) {
        ESP_LOGI(TAG, "=> GIẢI MÃ HẸN GIỜ THÀNH CÔNG: %s", acPlaintextBuffer);
        cJSON *jsParsed = cJSON_Parse(acPlaintextBuffer);
        if (jsParsed != NULL) {
            app_schedule_item_t sNewSchedule;
            (void)memset(&sNewSchedule, 0, sizeof(app_schedule_item_t));

            cJSON *jsId = cJSON_GetObjectItem(jsParsed, "id");
            cJSON *jsActivate = cJSON_GetObjectItem(jsParsed, "activate");
            sNewSchedule.u32Id = (jsId && cJSON_IsNumber(jsId)) ? (uint32_t)jsId->valueint : 0U;
            sNewSchedule.u8Activate = (jsActivate && cJSON_IsNumber(jsActivate)) ? (uint8_t)jsActivate->valueint : 1U;

            cJSON *jsCondition = cJSON_GetObjectItem(jsParsed, "condition");
            if (jsCondition) {
                cJSON *jsCondValues = cJSON_GetObjectItem(jsCondition, "values");
                cJSON *jsCondVal0 = cJSON_GetArrayItem(jsCondValues, 0);
                if (jsCondVal0) {
                    cJSON *jsLoopDays = cJSON_GetObjectItem(jsCondVal0, "loopDays");
                    sNewSchedule.u8LoopDays = (jsLoopDays && cJSON_IsNumber(jsLoopDays)) ? (uint8_t)jsLoopDays->valueint : 0U;

                    cJSON *jsExeTime = cJSON_GetObjectItem(jsCondVal0, "executionTime");
                    if (jsExeTime && cJSON_IsNumber(jsExeTime)) {
                        time_t tExecution = (time_t)jsExeTime->valueint;
                        struct tm sTimeInfo;
                        tExecution += (7 * 3600); // Bù UTC+7 (Việt Nam)
                        gmtime_r(&tExecution, &sTimeInfo);
                        
                        sNewSchedule.u8Hour = (uint8_t)sTimeInfo.tm_hour;
                        sNewSchedule.u8Minute = (uint8_t)sTimeInfo.tm_min;
                        ESP_LOGI(TAG, "-> Đặt lịch vào lúc: %02d:%02d (Loop: %d)", sNewSchedule.u8Hour, sNewSchedule.u8Minute, sNewSchedule.u8LoopDays);
                    }
                }
            }

            cJSON *jsAction = cJSON_GetObjectItem(jsParsed, "action");
            cJSON *jsAction0 = cJSON_GetArrayItem(jsAction, 0);
            if (jsAction0) {
                cJSON *jsDevV = cJSON_GetObjectItem(jsAction0, "devV");
                cJSON *jsDevV0 = cJSON_GetArrayItem(jsDevV, 0);
                if (jsDevV0) {
                    cJSON *jsParam = cJSON_GetObjectItem(jsDevV0, "param");
                    cJSON *jsValue = cJSON_GetObjectItem(jsDevV0, "value");
                    
                    if (jsParam && cJSON_IsString(jsParam)) {
                        snprintf(sNewSchedule.acParam, sizeof(sNewSchedule.acParam), "%s", jsParam->valuestring);
                    }
                    if (jsValue && cJSON_IsNumber(jsValue)) {
                        sNewSchedule.i32Value = (int32_t)jsValue->valueint;
                    }
                    ESP_LOGI(TAG, "-> Lệnh thực thi: %s = %d", sNewSchedule.acParam, (int)sNewSchedule.i32Value);
                }
            }

            esp_err_t err = app_nvs_SaveSchedule(&sNewSchedule);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Đã lưu lịch hẹn giờ vào NVS thành công!");
                app_logic_mqtt_publisher_ReportScheduleResult(acCmdNameCopy, sNewSchedule.u32Id, 50000);
            } else if (err == ESP_ERR_NO_MEM) {
                app_logic_mqtt_publisher_ReportScheduleResult(acCmdNameCopy, sNewSchedule.u32Id, 50007);
            } else {
                app_logic_mqtt_publisher_ReportScheduleResult(acCmdNameCopy, sNewSchedule.u32Id, 50005);
            }
            cJSON_Delete(jsParsed);
        }
    } else {
        ESP_LOGE(TAG, "Giải mã CmdAddSchedule thất bại, mã lỗi: %d", (int)eErr);
    }
}
/**
 * @brief   Giải mã và xử lý bản tin CmdDelSchedule (Xóa hẹn giờ)
 * @param   jsRoot Con trỏ cJSON trỏ tới gốc của gói tin JSON.
 */
static void app_logic_mqtt_HandleDeleteSchedule(const cJSON *jsRoot)
{
    if (jsRoot == NULL) return;

    int iId = 0;
    bool bHasId = false;

    const cJSON *jsEncryptArray = cJSON_GetObjectItem(jsRoot, "valueEncrypt");
    if (!cJSON_IsArray(jsEncryptArray)) {
        const cJSON *jsValue = cJSON_GetObjectItem(jsRoot, "value");
        if (jsValue != NULL) {
            jsEncryptArray = cJSON_GetObjectItem(jsValue, "valueEncrypt");
        }
    }

    /* 1. Lấy ID từ bản tin (Hỗ trợ cả 2 dạng: không mã hóa và mã hóa) */
    if (!cJSON_IsArray(jsEncryptArray)) {
        const cJSON *jsValue = cJSON_GetObjectItem(jsRoot, "value");
        if (jsValue != NULL) {
            cJSON *jsId = cJSON_GetObjectItem(jsValue, "id");
            if (cJSON_IsNumber(jsId)) {
                iId = jsId->valueint;
                bHasId = true;
            }
        }
    } else {
        /* (Tùy chọn: Giữ lại khối logic giải mã au8Ciphertext bằng mbedTLS ở đây nếu Server có mã hóa) */
        // ... (Code giải mã) ... 
    }

    /* 2. Xử lý xóa và phản hồi */
    if (bHasId) {
        esp_err_t eErr = ESP_OK;
        
        if (iId == -1) {
            ESP_LOGI(TAG, "-> Nhận lệnh xóa TẤT CẢ lịch hẹn giờ (id = -1)");
            eErr = app_nvs_DeleteAllSchedules();
        } else {
            eErr = app_nvs_DeleteSchedule((uint32_t)iId);
        }

        /* 3. Phản hồi kết quả về App */
        /* Mẹo: Dù NVS báo không tìm thấy lịch cũ (ESP_ERR_NOT_FOUND), 
           chúng ta vẫn phản hồi mã thành công 50000 để App gỡ giao diện chờ. */
        if (eErr == ESP_OK || eErr == ESP_ERR_NOT_FOUND) {
            (void)app_logic_mqtt_publisher_ReportScheduleResult("CmdDeleteSchedule", (uint32_t)iId, 50000);
            ESP_LOGI(TAG, "Đã gửi bản tin phản hồi xóa lịch (50000) thành công");
        } else {
            (void)app_logic_mqtt_publisher_ReportScheduleResult("CmdDeleteSchedule", (uint32_t)iId, 50005);
        }
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
                            app_logic_relay_UpdateAppUI();
                        } 
                        else if (strcmp(pcCmdName, "CmdGetWifiInfo") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh CmdGetWifiInfo");
                            (void)app_logic_mqtt_publisher_ReportWifiInfo();
                        } 
                        else if (strcmp(pcCmdName, "CmdGetExtraConfig") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh CmdGetExtraConfig");
                            /* 
                             * CẤP PHÁT TĨNH: Sử dụng từ khóa static để mảng lưu cố định trong vùng nhớ .bss/data, 
                             * không làm tràn bộ nhớ Stack của Task và tuyệt đối không dùng Heap (malloc).
                             */
                            static char acResponseJson[1536];
                            memset(acResponseJson, 0, sizeof(acResponseJson));
                            
                            /* Gọi module phụ trợ ghi dữ liệu trực tiếp vào mảng tĩnh */
                            app_logic_extra_config_ProcessGet(acResponseJson, sizeof(acResponseJson));
                            
                            if (acResponseJson[0] != '\0') {
                                /* TODO: Gọi hàm Publish MQTT thực tế của bạn tại đây */
                                (void)app_logic_mqtt_publisher_SendResponse(acResponseJson);
                            }
                        }else if (strcmp(pcCmdName, "CmdSetExtraConfig") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh CmdSetExtraConfig, cập nhật thông số...");
                            app_logic_extra_config_ProcessSet(jsValue ? jsValue : jsRoot); 
                        }else if(strcmp(pcCmdName, "CmdGetDeviceInfo") ==0){
                            ESP_LOGI(TAG, "-> khớp lệnh CmdGetDeviceInfo");
                            (void)app_logic_mqtt_publisher_ReportDeviceInfo();
                        } else if(strcmp(pcCmdName, "CmdGetSensorConfig") ==0){
                            ESP_LOGI(TAG, "-> khớp lệnh CmdGetSensorConfig");
                        }else if (strcmp(pcCmdName, "CmdSetData") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh CmdSetData, tiến hành gọi hàm giải mã...");
                            app_logic_mqtt_HandleSetData(jsValue);
                        }else if (strcmp(pcCmdName, "CmdAddSchedule") == 0 || strcmp(pcCmdName, "CmdUpdateSchedule") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh %s, đang giải mã...", pcCmdName);
                            /* Truyền địa chỉ của jsRoot để hàm con có thể xóa và gán NULL */
                            app_logic_mqtt_HandleAddAndUpdateSchedule(&jsRoot, pcCmdName);
                        }else if (strcmp(pcCmdName, "CmdDeleteSchedule") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh CmdDeleteSchedule, đang xử lý xóa...");
                            app_logic_mqtt_HandleDeleteSchedule(jsRoot);
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