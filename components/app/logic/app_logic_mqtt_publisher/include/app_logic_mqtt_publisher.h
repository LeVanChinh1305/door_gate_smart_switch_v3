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


esp_err_t app_logic_mqtt_publisher_ReportGateData(uint8_t u8Gate1, uint8_t u8Gate2, uint8_t arg3, uint8_t currentLevel);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOGIC_MQTT_PUBLISHER_H */