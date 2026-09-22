#include "app_logic_mqtt_publisher.h"
#include "app_mqtt.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "mqtt_vconnex_decrypt.h"
#include <sys/time.h>
#include "esp_wifi.h"
#include "app_wifi.h"
#include "app_logic_extra_config.h"

static const char *TAG = "APP_MQTT_PUB";

esp_err_t app_logic_mqtt_publisher_SendResponse(const char *pcPayload)
{
    if (pcPayload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 1. Lấy con trỏ client quản lý kết nối MQTT */
    esp_mqtt_client_handle_t xClient = app_mqtt_GetClient();
    if (xClient == NULL) {
        ESP_LOGE(TAG, "Lỗi: MQTT Client chưa được khởi tạo hoặc mất kết nối");
        return ESP_FAIL;
    }
    if (!app_mqtt_IsConnected()) { 
        ESP_LOGW(TAG, "MQTT đang mất kết nối, bỏ qua việc gửi bản tin");
        return ESP_FAIL;
    }

    /* 2. Lấy cấu hình thiết bị để trích xuất Topic Publish */
    const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
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

esp_err_t app_logic_mqtt_publisher_ReportGateData(uint8_t u8Gate1, uint8_t u8Gate2, uint8_t u8Gate3, uint8_t u8CurrentLevel, uint8_t u8GateOpenGap){
    const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
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
             "{\"param\":\"gate_open_gap\",\"value\":%u},"
             "{\"param\":\"sensor\",\"value\":0}]", // đang để mặc định 
                (unsigned int)u8Gate1, 
                (unsigned int)u8Gate2, 
                (unsigned int)u8Gate3, 
                (unsigned int)u8CurrentLevel, 
                (unsigned int)u8GateOpenGap);
    ESP_LOGI(TAG, "Bản tin gửi lên: %s", acPlaintext);

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

    /* 3. Dựng phần devVEncrypt array bằng snprintf — không dùng cJSON để tránh cấp phát 100+ node heap */
    /* Mỗi byte tốn tối đa 4 ký tự ("255,"), 512 bytes → tối đa 2048 + 2 dấu ngoặc */
    static char s_acEncryptArrayBuf[DF_MQTT_CRYPTO_MAX_BUFFER_SIZE * 4U + 4U];
    size_t zPos = 0;
    s_acEncryptArrayBuf[zPos++] = '[';
    for (size_t i = 0; i < zCipherLen; i++) {
        int iWritten = snprintf(&s_acEncryptArrayBuf[zPos],
                                sizeof(s_acEncryptArrayBuf) - zPos - 2U,
                                "%u%s", (unsigned int)au8Ciphertext[i],
                                (i + 1U < zCipherLen) ? "," : "");
        if (iWritten > 0) {
            zPos += (size_t)iWritten;
        }
    }
    s_acEncryptArrayBuf[zPos++] = ']';
    s_acEncryptArrayBuf[zPos]   = '\0';

    /* 4. Lấy Timestamp */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t u64Timestamp = (uint64_t)(tv.tv_sec) * 1000ULL + (uint64_t)(tv.tv_usec) / 1000ULL;

    /* 5. Dựng JSON cuối cùng bằng snprintf trên stack — không cần malloc/cJSON */
    char acResponseJson[DF_MQTT_CRYPTO_MAX_BUFFER_SIZE * 4U + 256U];
    snprintf(acResponseJson, sizeof(acResponseJson),
             "{"
             "\"name\":\"CmdGetData\","
             "\"devT\":%d,"
             "\"batteryPercent\":100,"
             "\"devExtAddr\":\"%s\","
             "\"timeStamp\":%llu,"
             "\"devVEncrypt\":%s"
             "}",
             (int)psConfig->dev_type,
             psConfig->dev_ext_addr,
             (unsigned long long)u64Timestamp,
             s_acEncryptArrayBuf);

    return app_logic_mqtt_publisher_SendResponse(acResponseJson);
}


esp_err_t app_logic_mqtt_publisher_ReportWifiInfo(void)
{
    const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
    if (psConfig == NULL || psConfig->mqtt_pub[0] == '\0') {
        return ESP_FAIL;
    }

    /* 1. Lấy thông tin WiFi từ hàm hệ thống ESP-IDF */
    wifi_ap_record_t sApInfo;
    char acSsid[33] = "DISCONNECTED";
    uint8_t u8RssiLevel = 0U;

    if (esp_wifi_sta_get_ap_info(&sApInfo) == ESP_OK) {
        snprintf(acSsid, sizeof(acSsid), "%s", sApInfo.ssid);
        
        /* 2. Quy đổi cường độ tín hiệu (dBm) sang thang điểm 0-4 vạch */
        if (sApInfo.rssi >= -55) {
            u8RssiLevel = 4U;
        } else if (sApInfo.rssi >= -70) {
            u8RssiLevel = 3U;
        } else if (sApInfo.rssi >= -80) {
            u8RssiLevel = 2U;
        } else if (sApInfo.rssi >= -90) {
            u8RssiLevel = 1U;
        } else {
            u8RssiLevel = 0U;
        }
    }

    /* 3. Lấy Timestamp */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t u64Timestamp = (uint64_t)(tv.tv_sec) * 1000ULL + (uint64_t)(tv.tv_usec) / 1000ULL;

    /* 4. Đóng gói JSON theo đúng chuẩn không mã hóa devV */
    char acResponse[512];
    snprintf(acResponse, sizeof(acResponse),
             "{"
             "\"name\":\"CmdGetWifiInfo\","
             "\"devT\":%u,"
             "\"devExtAddr\":\"%s\","
             "\"timeStamp\":%llu,"
             "\"devV\":["
             "{\"param\":\"wifi_name\",\"value\":\"%s\"},"
             "{\"param\":\"rssi\",\"value\":%u}"
             "]"
             "}",
             (unsigned int)psConfig->dev_type,
             psConfig->dev_ext_addr,
             (unsigned long long)u64Timestamp,
             acSsid,
             u8RssiLevel);

    /* 5. Đẩy bản tin lên broker */
    return app_logic_mqtt_publisher_SendResponse(acResponse);
}



esp_err_t app_logic_mqtt_publisher_ReportDeviceInfo(void)
{
    const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
    if (psConfig == NULL || psConfig->mqtt_pub[0] == '\0') {
        return ESP_FAIL;
    }

    /* 1. Lấy Timestamp hiện tại */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t u64Timestamp = (uint64_t)(tv.tv_sec) * 1000ULL + (uint64_t)(tv.tv_usec) / 1000ULL;

    /* 2. Đóng gói JSON (Sử dụng chuỗi phiên bản tĩnh) */
    char acResponse[512];
    snprintf(acResponse, sizeof(acResponse),
             "{"
             "\"name\":\"CmdGetDeviceInfo\","
             "\"devT\":%u,"
             "\"devExtAddr\":\"%s\","
             "\"timeStamp\":%llu,"
             "\"devV\":["
             "{\"param\":\"wifi_version\",\"value\":\"1.2\"},"
             "{\"param\":\"ble_version\",\"value\":\"1.1\"}"
             "]"
             "}",
             (unsigned int)psConfig->dev_type,
             psConfig->dev_ext_addr,
             (unsigned long long)u64Timestamp);

    /* 3. Gửi bản tin lên broker */
    return app_logic_mqtt_publisher_SendResponse(acResponse);
}


esp_err_t app_logic_mqtt_publisher_ReportScheduleResult(const char *pcCmdName, int32_t i32Id, int iErrorCode)
{
    const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
    if (psConfig == NULL || psConfig->mqtt_pub[0] == '\0') {
        return ESP_FAIL;
    }

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
             "\"id\":%d,"
             "\"errorCode\":%d"
             "}",
             pcCmdName,
             (unsigned int)psConfig->dev_type,
             psConfig->dev_ext_addr,
             (unsigned long long)u64Timestamp,
             (int)i32Id,
             iErrorCode);

    return app_logic_mqtt_publisher_SendResponse(acResponse);
}


esp_err_t app_logic_mqtt_publisher_ReportExtraConfig(void)
{
    const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
    if (psConfig == NULL || psConfig->mqtt_pub[0] == '\0') {
        ESP_LOGE(TAG, "Lỗi: Chưa có cấu hình MQTT Publisher");
        return ESP_FAIL;
    }

    /* 1. Khởi tạo cJSON Root Object */
    cJSON *jsRoot = cJSON_CreateObject();
    if (jsRoot == NULL) {
        return ESP_FAIL;
    }

    /* 2. Điền các thông tin định danh hệ thống */
    cJSON_AddStringToObject(jsRoot, "name", "CmdGetExtraConfig");
    cJSON_AddNumberToObject(jsRoot, "devT", psConfig->dev_type);
    cJSON_AddStringToObject(jsRoot, "devExtAddr", psConfig->dev_ext_addr);

    /* 3. Điền các trường cấu hình ngoại vi & LED */
    cJSON_AddNumberToObject(jsRoot, "buzzerEnb", g_sExtraConfig.buzzerEnb);
    cJSON_AddNumberToObject(jsRoot, "ledEnb", g_sExtraConfig.ledEnb);
    cJSON_AddNumberToObject(jsRoot, "ledRgbOn", g_sExtraConfig.ledRgbOn);
    cJSON_AddNumberToObject(jsRoot, "ledRgbOff", g_sExtraConfig.ledRgbOff);

    /* 4. Cấu hình chi tiết cho Cổng 1 (Gate 1) */
    cJSON_AddNumberToObject(jsRoot, "gate_1_type", g_sExtraConfig.gate_1_type);
    cJSON_AddNumberToObject(jsRoot, "gate_1_control_mode", g_sExtraConfig.gate_1_control_mode);
    cJSON_AddNumberToObject(jsRoot, "gate_1_led_off", g_sExtraConfig.gate_1_led_off);
    cJSON_AddNumberToObject(jsRoot, "gate_1_rgb_on", g_sExtraConfig.gate_1_rgb_on);
    cJSON_AddNumberToObject(jsRoot, "gate_1_rgb_off", g_sExtraConfig.gate_1_rgb_off);

    /* 5. Cấu hình chi tiết cho Cổng 2 (Gate 2) */
    cJSON_AddNumberToObject(jsRoot, "gate_2_type", g_sExtraConfig.gate_2_type);
    cJSON_AddNumberToObject(jsRoot, "gate_2_control_mode", g_sExtraConfig.gate_2_control_mode);
    cJSON_AddNumberToObject(jsRoot, "gate_2_led_off", g_sExtraConfig.gate_2_led_off);
    cJSON_AddNumberToObject(jsRoot, "gate_2_rgb_on", g_sExtraConfig.gate_2_rgb_on);
    cJSON_AddNumberToObject(jsRoot, "gate_2_rgb_off", g_sExtraConfig.gate_2_rgb_off);

    /* 6. Cấu hình chi tiết cho Cổng 3 (Gate 3) */
    cJSON_AddNumberToObject(jsRoot, "gate_3_type", g_sExtraConfig.gate_3_type);
    cJSON_AddNumberToObject(jsRoot, "gate_3_control_mode", g_sExtraConfig.gate_3_control_mode);
    cJSON_AddNumberToObject(jsRoot, "gate_3_led_off", g_sExtraConfig.gate_3_led_off);
    cJSON_AddNumberToObject(jsRoot, "gate_3_rgb_on", g_sExtraConfig.gate_3_rgb_on);
    cJSON_AddNumberToObject(jsRoot, "gate_3_rgb_off", g_sExtraConfig.gate_3_rgb_off);

    /* 7. Khung giờ ban đêm (Night Mode) */
    cJSON_AddNumberToObject(jsRoot, "nightModeEnb", g_sExtraConfig.nightModeEnb);
    cJSON_AddNumberToObject(jsRoot, "nightBegin", g_sExtraConfig.nightBegin);
    cJSON_AddNumberToObject(jsRoot, "nightEnd", g_sExtraConfig.nightEnd);
    cJSON_AddNumberToObject(jsRoot, "nightTz", g_sExtraConfig.nightTz);

    /* 8. Khung giờ cảnh báo (Warning Mode) */
    cJSON_AddNumberToObject(jsRoot, "warningEnb", g_sExtraConfig.warningEnb);
    cJSON_AddNumberToObject(jsRoot, "warningBegin", g_sExtraConfig.warningBegin);
    cJSON_AddNumberToObject(jsRoot, "warningEnd", g_sExtraConfig.warningEnd);

    /* 9. Độ sáng từng phím bấm & tổng thể */
    cJSON_AddNumberToObject(jsRoot, "gate_1_lightness", g_sExtraConfig.switch_1_lightness);
    cJSON_AddNumberToObject(jsRoot, "gate_2_lightness", g_sExtraConfig.switch_2_lightness);
    cJSON_AddNumberToObject(jsRoot, "gate_3_lightness", g_sExtraConfig.switch_3_lightness);
    cJSON_AddNumberToObject(jsRoot, "led_lightness", g_sExtraConfig.led_lightness);

    /* 10. Khóa Anti-Animal & Đếm ngược cửa */
    cJSON_AddNumberToObject(jsRoot, "anti_animal_enb", g_sExtraConfig.anti_animal_enb);
    cJSON_AddNumberToObject(jsRoot, "anti_animal_lock_time", g_sExtraConfig.anti_animal_lock_time);
    cJSON_AddNumberToObject(jsRoot, "gate_countdown", g_sExtraConfig.gate_countdown);

    /* 11. Chu kỳ SGM & Gap */
    cJSON_AddNumberToObject(jsRoot, "sgmCycle", g_sExtraConfig.sgmCycle);
    cJSON_AddNumberToObject(jsRoot, "sgmCycleGap", g_sExtraConfig.sgmCycleGap);
    cJSON_AddNumberToObject(jsRoot, "sgmUseCycleGap", g_sExtraConfig.sgmUseCycleGap);

    /* 12. Khóa ngoại vi (Lock RF) */
    cJSON_AddNumberToObject(jsRoot, "lockRFEnb", g_sExtraConfig.lockRFEnb);
    cJSON_AddNumberToObject(jsRoot, "lockRFBegin", g_sExtraConfig.lockRFBegin);
    cJSON_AddNumberToObject(jsRoot, "lockRFEnd", g_sExtraConfig.lockRFEnd);

    /* 13. Xuất chuỗi Unformatted và Publish qua MQTT */
    char *pcResponseJson = cJSON_PrintUnformatted(jsRoot);
    esp_err_t eRet = ESP_FAIL;
    
    if (pcResponseJson != NULL) {
        eRet = app_logic_mqtt_publisher_SendResponse(pcResponseJson);
        free(pcResponseJson);
    }
    
    cJSON_Delete(jsRoot);
    return eRet;
}


esp_err_t app_logic_mqtt_publisher_ReportStatus(void){
    const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
    if (psConfig == NULL || psConfig->mqtt_pub[0] == '\0') {
        return ESP_FAIL;
    }

     char acResponse[512];
    snprintf(acResponse, sizeof(acResponse),
             "{"
                "\"name\":\"CmdGetStatus\","
                "\"devT\":%u,"
                "\"devExtAddr\":\"%s\","
             "}",
             (unsigned int)psConfig->dev_type,
             psConfig->dev_ext_addr
            );

    /* 5. Đẩy bản tin lên broker */
    return app_logic_mqtt_publisher_SendResponse(acResponse);
}



esp_err_t app_logic_mqtt_publisher_ReportScheduleList(void)
{
    const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
    if (psConfig == NULL || psConfig->mqtt_pub[0] == '\0') {
        ESP_LOGE(TAG, "Lỗi: Cấu hình thiết bị hoặc MQTT pub topic không hợp lệ");
        return ESP_FAIL;
    }

    /* 1. Đọc danh sách lịch hiện có trong NVS */
    app_schedule_item_t asSchedules[DF_MAX_SCHEDULES];
    uint8_t u8Count = 0;
    (void)app_nvs_GetAllSchedules(asSchedules, &u8Count);

    /* 2. Tạo cJSON Root Object */
    cJSON *jsRoot = cJSON_CreateObject();
    if (jsRoot == NULL) {
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(jsRoot, "name", "CmdScheduleList");
    cJSON_AddNumberToObject(jsRoot, "devT", psConfig->dev_type);
    cJSON_AddStringToObject(jsRoot, "devExtAddr", psConfig->dev_ext_addr);

    /* Lấy Timestamp hiện tại (giây) */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t u64Timestamp = (uint64_t)tv.tv_sec;
    cJSON_AddNumberToObject(jsRoot, "timeStamp", (double)u64Timestamp);

    /* 3. Đóng gói mảng devV */
    cJSON *jsDevV = cJSON_CreateArray();
    if (jsDevV != NULL) {
        /* max_skd: Số lượng lịch tối đa thiết bị hỗ trợ */
        cJSON *jsMaxSkd = cJSON_CreateObject();
        cJSON_AddStringToObject(jsMaxSkd, "param", "max_skd");
        cJSON_AddNumberToObject(jsMaxSkd, "value", DF_MAX_SCHEDULES);
        cJSON_AddItemToArray(jsDevV, jsMaxSkd);

        /* skd_count: Số lượng lịch thực tế đang cài đặt */
        cJSON *jsSkdCount = cJSON_CreateObject();
        cJSON_AddStringToObject(jsSkdCount, "param", "skd_count");
        cJSON_AddNumberToObject(jsSkdCount, "value", u8Count);
        cJSON_AddItemToArray(jsDevV, jsSkdCount);

        /* Thêm từng ID lịch đang có */
        for (uint8_t i = 0; i < u8Count; i++) {
            cJSON *jsIdItem = cJSON_CreateObject();
            cJSON_AddStringToObject(jsIdItem, "param", "id");
            cJSON_AddNumberToObject(jsIdItem, "value", asSchedules[i].u32Id);
            cJSON_AddItemToArray(jsDevV, jsIdItem);
        }

        cJSON_AddItemToObject(jsRoot, "devV", jsDevV);
    }

    /* 4. Chuyển thành chuỗi Unformatted và Publish lên MQTT */
    char *pcResponseJson = cJSON_PrintUnformatted(jsRoot);
    esp_err_t eRet = ESP_FAIL;
    
    if (pcResponseJson != NULL) {
        eRet = app_logic_mqtt_publisher_SendResponse(pcResponseJson);
        free(pcResponseJson);
    }

    cJSON_Delete(jsRoot);
    return eRet;
}

esp_err_t app_logic_mqtt_publisher_ReportSensorConfig(const char *pcCmdName)
{
    const app_nvs_device_config_t *psConfig = app_mqtt_GetDeviceConfig();
    if (psConfig == NULL || psConfig->mqtt_pub[0] == '\0') {
        return ESP_FAIL;
    }

    const char *pcResponseName = (pcCmdName != NULL) ? pcCmdName : "CmdGetSensorConfig";

    app_nvs_sensor_config_t sSensorCfg;
    if (app_nvs_GetSensorConfig(&sSensorCfg) != ESP_OK) {
        memset(&sSensorCfg, 0, sizeof(sSensorCfg));
    }

    cJSON *jsRoot = cJSON_CreateObject();
    if (jsRoot == NULL) return ESP_FAIL;

    /* Đưa trực tiếp tất cả các trường ra root JSON */
    cJSON_AddStringToObject(jsRoot, "name", pcResponseName);
    cJSON_AddNumberToObject(jsRoot, "devT", psConfig->dev_type);
    cJSON_AddStringToObject(jsRoot, "devExtAddr", psConfig->dev_ext_addr);
    cJSON_AddNumberToObject(jsRoot, "sensorType", sSensorCfg.u8SensorType);

    /* Mảng MAC sensor (nếu là loại BLE) */
    if (sSensorCfg.u8SensorType == 2) {
        cJSON *jsMacArray = cJSON_CreateArray();
        for (int i = 0; i < 6; i++) {
            cJSON_AddItemToArray(jsMacArray, cJSON_CreateNumber(sSensorCfg.au8SensorMac[i]));
        }
        cJSON_AddItemToObject(jsRoot, "sensorMac", jsMacArray);
        cJSON_AddNumberToObject(jsRoot, "pairMode", sSensorCfg.u8PairMode);
    }

    /* Chi gửi sensorReset nếu bằng 1 */
    if (sSensorCfg.u8SensorReset == 1) {
        cJSON_AddNumberToObject(jsRoot, "sensorReset", 1);
    }

    if (sSensorCfg.u8SensorType == 1) {
        sSensorCfg.u8SensorAntiStuck = 0;
    }

    cJSON_AddNumberToObject(jsRoot, "sensorAntiStuck", sSensorCfg.u8SensorAntiStuck);
    cJSON_AddNumberToObject(jsRoot, "sensorAction", sSensorCfg.u8SensorAction);
    cJSON_AddNumberToObject(jsRoot, "sensorWarning", sSensorCfg.u8SensorWarning);

    char *pcResponseJson = cJSON_PrintUnformatted(jsRoot);
    esp_err_t eRet = ESP_FAIL;
    if (pcResponseJson != NULL) {
        ESP_LOGI(TAG, "=> ReportSensorConfig JSON phẳng: %s", pcResponseJson);
        eRet = app_logic_mqtt_publisher_SendResponse(pcResponseJson);
        free(pcResponseJson);
    }
    cJSON_Delete(jsRoot);
    return eRet;
}