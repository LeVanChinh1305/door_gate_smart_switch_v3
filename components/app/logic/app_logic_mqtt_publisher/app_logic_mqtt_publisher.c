#include "app_logic_mqtt_publisher.h"
#include "app_mqtt.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "mqtt_vconnex_decrypt.h"
#include "cJSON.h"
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

esp_err_t app_logic_mqtt_publisher_ReportGateData(uint8_t u8Gate1, uint8_t u8Gate2, uint8_t u8Gate3, uint8_t u8CurrentLevel){
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
             "{\"param\":\"wifi_version\",\"value\":\"1.1\"},"
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