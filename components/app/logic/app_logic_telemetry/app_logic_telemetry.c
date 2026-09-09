/**
 * @file    app_logic_telemetry.c
 * @brief   Triển khai đóng gói và báo cáo telemetry/lịch sử từ Thiết bị lên Cloud Backend Vconnex.
 */

#include "app_logic_telemetry.h"
#include "app_mqtt.h"
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include <time.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "APP_LOGIC_TELEMETRY";

/**
 * @brief Lấy Unix Timestamp hiện tại bằng giây
 */
static int64_t get_current_timestamp_ms(void)
{
    time_t tNow = 0;
    (void)time(&tNow);
    return (int64_t)tNow;
}

/**
 * @brief   Hàm tiện ích giúp đóng gói nhanh 1 item ReportControlHistory chuẩn Spec Vconnex.
 * @details Tự động quy định desId = "" khi nguồn phát sinh lệnh thuộc về phần cứng thiết bị.
 */
void app_logic_telemetry_BuildControlItem(app_logic_control_history_item_t *pOutItem,
                                          const char *pcControl,
                                          e_telemetry_control_mode_t eMode,
                                          e_telemetry_control_src_t eSrc,
                                          const char *pcRawSrcId,
                                          const char *pcRawDesId)
{
    if (pOutItem == NULL) {
        return;
    }

    (void)memset(pOutItem, 0, sizeof(app_logic_control_history_item_t));

    /* 1. Gán Tên cổng/kênh (control), Mode và Nguồn (src) */
    (void)snprintf(pOutItem->cControl, sizeof(pOutItem->cControl), "%s", (pcControl != NULL) ? pcControl : "gate_1");
    pOutItem->i32Mode = (int32_t)eMode;
    pOutItem->i32Src = (int32_t)eSrc;

    /* 2. Lấy thời gian thực thi (time) theo chuẩn Unix Timestamp (ms) */
    pOutItem->i64Time = get_current_timestamp_ms();

    /* 3. Xử lý trường srcId */
    if ((pcRawSrcId != NULL) && (strlen(pcRawSrcId) > 0U)) {
        (void)snprintf(pOutItem->cSrcId, sizeof(pOutItem->cSrcId), "%s", pcRawSrcId);
    } else {
        const app_nvs_device_config_t *pDevCfg = app_mqtt_GetDeviceConfig();
        (void)snprintf(pOutItem->cSrcId, sizeof(pOutItem->cSrcId), "%s", (pDevCfg != NULL) ? pDevCfg->dev_ext_addr : "");
    }

    /* 4. Tự động chuẩn hóa desId theo quy định Spec Vconnex */
    switch (eSrc) {
        case E_TELEMETRY_SRC_PHYSICAL_DEVICE:
        case E_TELEMETRY_SRC_BLE_BACKUP:
        case E_TELEMETRY_SRC_DEVICE_SCHEDULE:
        case E_TELEMETRY_SRC_ANTI_COLLISION:
        case E_TELEMETRY_SRC_MAIKA:
        case E_TELEMETRY_SRC_GOOGLE_HOME:
        case E_TELEMETRY_SRC_ALEXA:
            /* Lệnh xuất phát từ thiết bị/cảm biến/loa nội bộ -> desId bắt buộc phải để chuỗi rỗng "" */
            pOutItem->cDesId[0] = '\0';
            break;

        default:
            /* Lệnh xuất phát từ App/Cloud/Gateway -> Gán desId truyền từ tham số */
            if (pcRawDesId != NULL) {
                (void)snprintf(pOutItem->cDesId, sizeof(pOutItem->cDesId), "%s", pcRawDesId);
            } else {
                pOutItem->cDesId[0] = '\0';
            }
            break;
    }
}

esp_err_t app_logic_telemetry_ReportControlHistory(const app_logic_control_history_item_t *pHistoryList, uint16_t u16ItemCount)
{
    if ((pHistoryList == NULL) || (u16ItemCount == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    const app_nvs_device_config_t *pDevCfg = app_mqtt_GetDeviceConfig();
    esp_mqtt_client_handle_t xMqttClient = app_mqtt_GetClient();

    if ((xMqttClient == NULL) || !app_mqtt_IsConnected()) {
        ESP_LOGE(TAG, "MQTT chưa kết nối, hủy gửi ReportControlHistory");
        return ESP_FAIL;
    }

    cJSON *pRoot = cJSON_CreateObject();
    if (pRoot == NULL) {
        return ESP_ERR_NO_MEM;
    }

    (void)cJSON_AddStringToObject(pRoot, "name", "ReportControlHistory");
    (void)cJSON_AddNumberToObject(pRoot, "devT", (double)pDevCfg->dev_type);
    (void)cJSON_AddStringToObject(pRoot, "devExtAddr", pDevCfg->dev_ext_addr);
    (void)cJSON_AddNumberToObject(pRoot, "timeStamp", (double)get_current_timestamp_ms());

    cJSON *pDevVArray = cJSON_CreateArray();
    if (pDevVArray != NULL) {
        for (uint16_t i = 0U; i < u16ItemCount; i++) {
            cJSON *pItem = cJSON_CreateObject();
            if (pItem != NULL) {
                (void)cJSON_AddStringToObject(pItem, "control", pHistoryList[i].cControl);
                (void)cJSON_AddNumberToObject(pItem, "mode", (double)pHistoryList[i].i32Mode);
                (void)cJSON_AddNumberToObject(pItem, "value", (double)pHistoryList[i].i32Value);
                (void)cJSON_AddNumberToObject(pItem, "time", (double)pHistoryList[i].i64Time);
                (void)cJSON_AddNumberToObject(pItem, "src", (double)pHistoryList[i].i32Src);
                (void)cJSON_AddStringToObject(pItem, "srcId", pHistoryList[i].cSrcId);
                (void)cJSON_AddStringToObject(pItem, "desId", pHistoryList[i].cDesId);
                cJSON_AddItemToArray(pDevVArray, pItem);
            }
        }
        cJSON_AddItemToObject(pRoot, "devV", pDevVArray);
    }

    char *pJsonOut = cJSON_PrintUnformatted(pRoot);
    cJSON_Delete(pRoot);

    if (pJsonOut == NULL) {
        ESP_LOGE(TAG, "Tạo JSON ReportControlHistory thất bại");
        return ESP_FAIL;
    }
    char acTopic[256];
    (void)snprintf(acTopic, sizeof(acTopic), "%s/ControlLog", pDevCfg->mqtt_alert);

    int i32MsgId = esp_mqtt_client_publish(xMqttClient, acTopic, pJsonOut, 0, 1, 0);
    if (i32MsgId >= 0) {
        ESP_LOGI(TAG, "Đã gửi ReportControlHistory (msg_id=%d) lên topic [%s]: %s", i32MsgId, pJsonOut, acTopic);
    } else {
        ESP_LOGE(TAG, "Gửi ReportControlHistory thất bại (msg_id=%d)", i32MsgId);
    }

    cJSON_free(pJsonOut);
    return (i32MsgId >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t app_logic_telemetry_ReportSensorHistory(const app_logic_sensor_history_item_t *pHistoryList, uint16_t u16ItemCount)
{
    if ((pHistoryList == NULL) || (u16ItemCount == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    const app_nvs_device_config_t *pDevCfg = app_mqtt_GetDeviceConfig();
    esp_mqtt_client_handle_t xMqttClient = app_mqtt_GetClient();

    if ((xMqttClient == NULL) || !app_mqtt_IsConnected()) {
        ESP_LOGE(TAG, "MQTT chưa kết nối, hủy gửi ReportSensorHistory");
        return ESP_FAIL;
    }

    cJSON *pRoot = cJSON_CreateObject();
    if (pRoot == NULL) {
        return ESP_ERR_NO_MEM;
    }
    int64_t i64CurrentMs = get_current_timestamp_ms(); //khai báo biến thời gian hiện tại

    (void)cJSON_AddStringToObject(pRoot, "name", "ReportSensorHistory");
    (void)cJSON_AddNumberToObject(pRoot, "devT", (double)pDevCfg->dev_type);
    (void)cJSON_AddStringToObject(pRoot, "devExtAddr", pDevCfg->dev_ext_addr);
    (void)cJSON_AddNumberToObject(pRoot, "timeStamp", (double)get_current_timestamp_ms());

    cJSON *pDevVArray = cJSON_CreateArray();
    if (pDevVArray != NULL) {
        for (uint16_t i = 0U; i < u16ItemCount; i++) {
            cJSON *pItem = cJSON_CreateObject();
            if (pItem != NULL) {
                int64_t i64EventTime = (pHistoryList[i].i64Time > 0) ? pHistoryList[i].i64Time : i64CurrentMs;
                (void)cJSON_AddNumberToObject(pItem, "sensor", (double)pHistoryList[i].u8SensorState);
                (void)cJSON_AddNumberToObject(pItem, "time", (double)i64EventTime);
                cJSON_AddItemToArray(pDevVArray, pItem);
            }
        }
        cJSON_AddItemToObject(pRoot, "devV", pDevVArray);
    }

    char *pJsonOut = cJSON_PrintUnformatted(pRoot);
    cJSON_Delete(pRoot);

    if (pJsonOut == NULL) {
        ESP_LOGE(TAG, "Tạo JSON ReportSensorHistory thất bại");
        return ESP_FAIL;
    }

    int i32MsgId = esp_mqtt_client_publish(xMqttClient, pDevCfg->mqtt_alert, pJsonOut, 0, 1, 0);
    if (i32MsgId >= 0) {
        ESP_LOGI(TAG, "Đã gửi ReportSensorHistory (msg_id=%d): %s", i32MsgId, pJsonOut);
    } else {
        ESP_LOGE(TAG, "Gửi ReportSensorHistory thất bại (msg_id=%d)", i32MsgId);
    }

    cJSON_free(pJsonOut);
    return (i32MsgId >= 0) ? ESP_OK : ESP_FAIL;
}