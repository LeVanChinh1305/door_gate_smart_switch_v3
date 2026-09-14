/**
 * @file app_ota.h
 * @brief Quản lý cập nhật OTA cho công tắc cửa cổng Vconnex V3
 */

#ifndef APP_OTA_H
#define APP_OTA_H

#include "esp_err.h"
#include "cJSON.h"

/**
 * @brief Trạng thái cập nhật OTA
 */
typedef enum {
    E_OTA_STATE_IDLE = 0,
    E_OTA_STATE_DOWNLOADING,
    E_OTA_STATE_SUCCESS,
    E_OTA_STATE_FAILED
} e_ota_state_t;

/**
 * @brief Xử lý gói tin CmdStartOta từ MQTT
 * @param jsValue Object cJSON của trường "value" hoặc toàn bộ root JSON
 * @return esp_err_t ESP_OK nếu tạo task nạp OTA thành công
 */
esp_err_t app_ota_ProcessCmdStartOta(const cJSON *jsValue);

/**
 * @brief Lấy trạng thái OTA hiện tại
 */
e_ota_state_t app_ota_GetState(void);

#endif /* APP_OTA_H */