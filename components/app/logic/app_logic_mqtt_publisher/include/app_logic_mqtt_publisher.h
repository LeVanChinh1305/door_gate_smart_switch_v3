/**
 * @file    app_logic_mqtt_publisher.h
 * @brief   Xử lý logic đẩy bản tin (Publish) lên MQTT Broker.
 */

#ifndef APP_LOGIC_MQTT_PUBLISHER_H
#define APP_LOGIC_MQTT_PUBLISHER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Gửi bản tin JSON phản hồi hoặc báo cáo trạng thái lên MQTT Broker.
 * @param   pcPayload Nội dung bản tin định dạng JSON.
 * @return  ESP_OK nếu đưa vào hàng đợi gửi thành công.
 */
esp_err_t app_logic_mqtt_publisher_SendResponse(const char *pcPayload);


esp_err_t app_logic_mqtt_publisher_ReportGateData(uint8_t u8Gate1, uint8_t u8Gate2, uint8_t arg3, uint8_t currentLevel, uint8_t u8GateOpenGap);


/**
 * @brief Lấy thông tin WiFi hiện hành và phản hồi lệnh CmdGetWifiInfo lên MQTT
 * @return esp_err_t ESP_OK nếu thành công
 */
esp_err_t app_logic_mqtt_publisher_ReportWifiInfo(void);


/**
 * @brief Đóng gói và phản hồi thông tin phiên bản thiết bị (WiFi, BLE) cho lệnh CmdGetDeviceInfo
 * @return esp_err_t ESP_OK nếu đẩy bản tin thành công
 */
esp_err_t app_logic_mqtt_publisher_ReportDeviceInfo(void);


/**
 * @brief Gửi phản hồi kết quả Thêm/Xóa lịch hẹn giờ
 */
esp_err_t app_logic_mqtt_publisher_ReportScheduleResult(const char *pcCmdName, int32_t i32Id, int iErrorCode);


esp_err_t app_logic_mqtt_publisher_ReportExtraConfig(void);


esp_err_t app_logic_mqtt_publisher_ReportStatus(void);


esp_err_t app_logic_mqtt_publisher_ReportScheduleList(void);
#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_MQTT_PUBLISHER_H */