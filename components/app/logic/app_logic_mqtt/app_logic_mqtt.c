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
#include <sys/time.h>
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include "app_logic_relay.h"
#include "app_led_state.h"
#include "app_logic_mqtt_publisher.h" 
#include "app_logic_extra_config.h"
#include <time.h>
#include "app_logic_telemetry.h"
#include "app_nvs.h"
#include "esp_task_wdt.h"
#include "app_ota.h"

static const char *TAG = "APP_LOGIC_MQTT";

/* Biến toàn cục tĩnh quản lý hàng đợi nội bộ module */
static QueueHandle_t g_xMqttQueue = NULL;

/**
 * @brief   Giải mã và xử lý bản tin CmdSetData có chứa trường mã hóa devVEncrypt.
 * @param   jsRoot
 */
static void app_logic_mqtt_HandleSetData(const cJSON *jsRoot)
{
    if (jsRoot == NULL) {
        return;
    }

    const cJSON *pValue = cJSON_GetObjectItem(jsRoot, "value");

    if (pValue == NULL) {
        pValue = jsRoot;
    }
    /* 2. lấy srcId trực tiếp từ value */
    char acSrcId[64] = "0";
    const cJSON *jsSrcId = cJSON_GetObjectItem(pValue, "srcId");
    if ((jsSrcId == NULL) && (jsRoot != NULL)) {
        jsSrcId = cJSON_GetObjectItem(jsRoot, "srcId");
    }

    // 3. lấy mảng mã hóa 
    if (jsSrcId != NULL) {
        if (cJSON_IsString(jsSrcId) && (jsSrcId->valuestring != NULL)) {
            (void)snprintf(acSrcId, sizeof(acSrcId), "%s", jsSrcId->valuestring);
        } else if (cJSON_IsNumber(jsSrcId)) {
            (void)snprintf(acSrcId, sizeof(acSrcId), "%lld", (long long)jsSrcId->valueint);
        }
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

    /* 1. CHUẨN HÓA: Cấp phát mảng Ciphertext trên Stack (Loại bỏ malloc/free hoàn toàn) */
    uint8_t au8Ciphertext[DF_MQTT_CRYPTO_MAX_BUFFER_SIZE];
    (void)memset(au8Ciphertext, 0, sizeof(au8Ciphertext));

    for (int i = 0; i < iCipherLen; i++) {
        const cJSON *jsItem = cJSON_GetArrayItem(jsEncryptArray, i);
        au8Ciphertext[i] = cJSON_IsNumber(jsItem) ? (uint8_t)jsItem->valueint : 0U;
    }

    /* 2. Lấy cấu hình thiết bị để trích xuất khóa bí mật api_secret_key */
    const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
    if (psConfig == NULL || strlen(psConfig->api_secret_key) < 32U) {
        ESP_LOGE(TAG, "API Secret Key không hợp lệ hoặc chưa được cấu hình trong NVS");
        return;
    }

    /* 3. Sử dụng bộ đệm tĩnh đầu ra theo chuẩn hàm giải mã */
    char acPlaintextBuffer[DF_MQTT_CRYPTO_MAX_BUFFER_SIZE];
    size_t zPlaintextLen = 0U;
    (void)memset(acPlaintextBuffer, 0, sizeof(acPlaintextBuffer));

    esp_err_t eErr = decrypt_vconnex_payload(au8Ciphertext, 
                                            (size_t)iCipherLen,
                                            psConfig->api_secret_key,
                                            acPlaintextBuffer,
                                            sizeof(acPlaintextBuffer),
                                            &zPlaintextLen);

    if (eErr == ESP_OK && zPlaintextLen > 0U) {
        ESP_LOGI(TAG, "Giải mã payload thành công, nội dung: %s", acPlaintextBuffer);

        /* 4. Phân tích nội dung JSON bên trong chuỗi plaintext sau giải mã */
        cJSON *jsInnerJson = cJSON_Parse(acPlaintextBuffer);
        if (jsInnerJson != NULL) {
            const cJSON *jsParam = cJSON_GetObjectItem(jsInnerJson, "param");
            const cJSON *jsValue = cJSON_GetObjectItem(jsInnerJson, "value");

            if (cJSON_IsString(jsParam) && jsParam->valuestring != NULL) {
                const char *pcParam = jsParam->valuestring;
                int iValue = cJSON_IsNumber(jsValue) ? jsValue->valueint : 0;

                ESP_LOGI(TAG, "Điều khiển thiết bị - Param: %s, Value: %d", pcParam, iValue);

                //khai báo biến sLog cho gửi cloud
                app_logic_control_history_item_t sLog;
                (void)memset(&sLog, 0, sizeof(app_logic_control_history_item_t));

                /* Ánh xạ tham số từ app/cloud xuống lệnh điều khiển relay thực tế */
                if (strcmp(pcParam, "gate_3") == 0 || strcmp(pcParam, "up") == 0) {
                    if (iValue == 1) {
                        (void)app_logic_relay_Open();
                        ESP_LOGI(TAG, "-> Thực thi lệnh: Mở cửa (UP)");
                        (void)app_led_state_SetState(E_LED_STATE_GATE_UP);
                        // gửi lại lệnh lịch sử điều khiển 
                        app_logic_telemetry_BuildControlItem(&sLog, "gate_3", E_TELEMETRY_MODE_OPEN, E_TELEMETRY_SRC_APP, acSrcId, psConfig->dev_ext_addr);
                        // đóng gói cái trên thành sLog để dưới gửi 
                        (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);
                    }
                } 
                else if (strcmp(pcParam, "gate_1") == 0 || strcmp(pcParam, "down") == 0) {
                    if (iValue == 1) {
                        (void)app_logic_relay_Close();
                        ESP_LOGI(TAG, "-> Thực thi lệnh: Đóng cửa (DOWN)");
                        (void)app_led_state_SetState(E_LED_STATE_GATE_DOWN);
                        app_logic_telemetry_BuildControlItem(&sLog, "gate_1", E_TELEMETRY_MODE_CLOSE, E_TELEMETRY_SRC_APP, acSrcId, psConfig->dev_ext_addr);
                        (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);
                    }
                } 
                else if (strcmp(pcParam, "gate_2") == 0 || strcmp(pcParam, "stop") == 0) {
                    if (iValue == 1) {
                        (void)app_logic_relay_Stop();
                        ESP_LOGI(TAG, "-> Thực thi lệnh: Dừng cửa (STOP)");
                        (void)app_led_state_SetState(E_LED_STATE_GATE_STOP);
                        app_logic_telemetry_BuildControlItem(&sLog, "gate_2", E_TELEMETRY_MODE_STOP, E_TELEMETRY_SRC_APP, acSrcId, psConfig->dev_ext_addr);
                        (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);
                    }
                } 
                else if (strcmp(pcParam, "gate_level") == 0) {
                    if (iValue >= 0 && iValue <= 100) {
                        uint8_t u8TargetVal = (uint8_t)iValue;
                        uint8_t u8CurrentVal = app_logic_relay_GetCurrentLevel();
                        
                        ESP_LOGI(TAG, "-> Thực thi lệnh: Điều khiển cửa đến mức %u%% (Hiện tại: %u%%)", u8TargetVal, u8CurrentVal);
                        
                        if (u8TargetVal > u8CurrentVal) {
                            (void)app_led_state_SetState(E_LED_STATE_GATE_UP);
                        } else if (u8TargetVal < u8CurrentVal) {
                            (void)app_led_state_SetState(E_LED_STATE_GATE_DOWN);
                        } else {
                            (void)app_led_state_SetState(E_LED_STATE_GATE_STOP);
                        }

                        app_logic_relay_SetLevel(u8TargetVal);
                        app_logic_telemetry_BuildControlItem(&sLog, "open_level", E_TELEMETRY_MODE_PERCENT, E_TELEMETRY_SRC_APP, acSrcId, psConfig->dev_ext_addr);
                        sLog.i32Value = (int32_t)u8TargetVal;
                        (void)app_logic_telemetry_ReportControlHistory(&sLog, 1);
                    } else {
                        ESP_LOGW(TAG, "Giá trị gate_level không hợp lệ: %d", iValue);
                    }
                }else if(strcmp(pcParam, "gate_open_gap") == 0){
                    if(iValue == 1){
                        ESP_LOGI(TAG, "-> Khớp lệnh mở khe thoáng (gate_open_gap)");
                        app_logic_relay_OpenVentilationGap();
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
    const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
    if (psConfig == NULL || strlen(psConfig->api_secret_key) < 32U) {
        ESP_LOGE(TAG, "Lỗi API Secret Key không hợp lệ");
        return;
    }

    char acPlaintextBuffer[DF_MQTT_CRYPTO_MAX_BUFFER_SIZE];
    size_t zPlaintextLen = 0U;
    (void)memset(acPlaintextBuffer, 0, sizeof(acPlaintextBuffer));

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
            (void)app_logic_mqtt_publisher_ReportScheduleResult("CmdDeleteSchedule", (int32_t)iId, 50000);
        } else {
            (void)app_logic_mqtt_publisher_ReportScheduleResult("CmdDeleteSchedule", (int32_t)iId, 50005);
        }
    }
}

/**
 * @brief trả lại lệnh delete endpoint
 */
static void app_logic_mqtt_HandleDeleteEndpointConfig(const char *pcCmdName)
{
    const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
    if (psConfig == NULL) return;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t u64Timestamp = (uint64_t)(tv.tv_sec) * 1000ULL + (uint64_t)(tv.tv_usec) / 1000ULL;

    char acResponse[256];
    snprintf(acResponse, sizeof(acResponse),
             "{"
             "\"name\":\"%s\","
             "\"devT\":%u,"
             "\"devExtAddr\":\"%s\","
             "\"timestamp\":%llu,"
             "\"errorCode\":50000"
             "}",
             pcCmdName,
             (unsigned int)psConfig->dev_type,
             psConfig->dev_ext_addr,
             (unsigned long long)u64Timestamp);

    (void)app_logic_mqtt_publisher_SendResponse(acResponse);
    ESP_LOGI(TAG, "Đã phản hồi thành công lệnh %s", pcCmdName);
}


/**
 * @brief 
 */
static void app_logic_mqtt_HandleDeleteDevice(const char *pcCmdName)
{
    /* 1. Lấy thông tin thiết bị từ RAM/NVS để phản hồi */
    const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
    if (psConfig == NULL) return;

    /* 2. Đóng gói JSON phản hồi rút gọn theo Spec */
    char acResponse[192];
    snprintf(acResponse, sizeof(acResponse),
             "{"
             "\"name\":\"%s\","
             "\"devT\":%u,"
             "\"devExtAddr\":\"%s\""
             "}",
             pcCmdName,
             (unsigned int)psConfig->dev_type,
             psConfig->dev_ext_addr);

    /* 3. Đẩy bản tin phản hồi lên Broker */
    (void)app_logic_mqtt_publisher_SendResponse(acResponse);
    ESP_LOGI(TAG, "Đã gửi bản tin phản hồi: %s", acResponse);

    /* 4. Trễ 1 giây để TCP/TLS kịp truyền bản tin ra mạng */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* 5. Tiến hành xóa sạch NVS */
    ESP_LOGI(TAG, "Đang xóa NVS và khởi động lại...");
    app_nvs_DeleteAllSchedules();
    app_nvs_ClearExtraConfig();
    app_nvs_ClearDeviceConfig();
    app_nvs_ClearWifiConfig();

    /* 6. Reboot thiết bị */
    esp_restart();
}

/**
 * @brief   Task nền chuyên trách nhận bản tin từ Queue, phân loại lệnh và điều phối xử lý.
 * @param   pArg Tham số truyền vào task (không sử dụng).
 */
static void app_logic_mqtt_Task(void *pArg)
{
    DF_UNUSED(pArg);
    app_logic_mqtt_queue_item_t sItem;

    /* Đăng ký task MQTT vào TWDT */
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    while (true) {
        esp_task_wdt_reset();

        /* Chờ bản tin từ Queue tối đa 2 giây để định kỳ feed TWDT */
        if (xQueueReceive(g_xMqttQueue, &sItem, pdMS_TO_TICKS(2000U)) == pdPASS) {
            if ( sItem.u32DataLen > 0U) {
                /* Parse chuỗi JSON payload nhận được từ hàng đợi */
                cJSON *jsRoot = cJSON_Parse(sItem.acData);
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
                            app_logic_mqtt_HandleSetData(jsRoot);
                        }else if (strcmp(pcCmdName, "CmdAddSchedule") == 0 || strcmp(pcCmdName, "CmdUpdateSchedule") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh %s, đang giải mã...", pcCmdName);
                            /* Truyền địa chỉ của jsRoot để hàm con có thể xóa và gán NULL */
                            app_logic_mqtt_HandleAddAndUpdateSchedule(&jsRoot, pcCmdName);
                        }else if (strcmp(pcCmdName, "CmdDeleteSchedule") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh CmdDeleteSchedule, đang xử lý xóa...");
                            app_logic_mqtt_HandleDeleteSchedule(jsRoot);
                        }else if (strcmp(pcCmdName, "CmdDeleteEndpointConfig") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh CmdDeleteEndpointConfig");
                            app_logic_mqtt_HandleDeleteEndpointConfig(pcCmdName);
                        }else if (strcmp(pcCmdName, "CmdDeleteDevice") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh CmdDeleteDevice, tiến hành xóa thiết bị...");
                            app_logic_mqtt_HandleDeleteDevice(pcCmdName); 
                        }else if (strcmp(pcCmdName, "CmdStartOta") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh CmdStartOta, bắt đầu tiến trình OTA...");
                            esp_err_t eOtaRet = app_ota_ProcessCmdStartOta(jsValue ? jsValue : jsRoot);
                            
                            /* Phản hồi bản tin ACK về broker theo đúng chuẩn Vconnex (50000: Thành công, 50004: Thất bại) */
                            int i32ErrorCode = (eOtaRet == ESP_OK) ? 50000 : 50004;
                            (void)app_logic_mqtt_publisher_ReportScheduleResult("CmdStartOta", 0, i32ErrorCode);
                        }else if(strcmp(pcCmdName, "CmdGetStatus") ==0){
                            ESP_LOGI(TAG, "-> khớp lệnh CmdGetStatus");
                            (void)app_logic_mqtt_publisher_ReportStatus();
                        }else if (strcmp(pcCmdName, "CmdScheduleList") == 0) {
                            ESP_LOGI(TAG, "-> Khớp lệnh CmdScheduleList, phản hồi danh sách lịch hẹn...");
                            (void)app_logic_mqtt_publisher_ReportScheduleList();
                        }else {
                            ESP_LOGW(TAG, "-> Lệnh MQTT chưa được định nghĩa: %s", pcCmdName);
                        }
                    } else {
                        ESP_LOGW(TAG, "Không tìm thấy trường 'name' hoặc 'name' không phải là chuỗi trong JSON");
                    }
                    cJSON_Delete(jsRoot);
                } else {
                    ESP_LOGE(TAG, "cJSON_Parse thất bại với chuỗi payload: %s", sItem.acData);
                }
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
                                  DF_TASK_STACK_MAX, 
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
/**
 * @brief   Đưa dữ liệu bản tin thô vào hàng đợi an toàn không gây nghẽn ngắt mạng.
 */
esp_err_t app_logic_mqtt_EnqueueData(const char *pcData, uint32_t u32DataLen)
{
    if (g_xMqttQueue == NULL || pcData == NULL || u32DataLen == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    /* KIỂM TRA MỚI: Bỏ qua nếu payload vượt quá kích thước bộ đệm cho phép */
    if (u32DataLen >= DF_APP_MQTT_MAX_PAYLOAD_SIZE) {
        ESP_LOGE(TAG, "Gói tin MQTT vượt quá kích thước đệm tĩnh (%u bytes)", (unsigned int)u32DataLen);
        return ESP_ERR_NO_MEM;
    }

    app_logic_mqtt_queue_item_t sItem;
    sItem.u32DataLen = u32DataLen;
    
    /* SỬA ĐỔI: Copy chuỗi vào mảng tĩnh acData, loại bỏ malloc */
    (void)memcpy(sItem.acData, pcData, (size_t)u32DataLen);
    sItem.acData[u32DataLen] = '\0';

    /* Đưa vào Queue với timeout = 0 để tránh block luồng mạng lõi */
    if (xQueueSend(g_xMqttQueue, &sItem, 0U) != pdPASS) {
        ESP_LOGW(TAG, "Hàng đợi MQTT đã đầy, bỏ qua bản tin!");
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}