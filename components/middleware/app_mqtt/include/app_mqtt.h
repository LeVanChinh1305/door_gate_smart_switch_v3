#pragma once 

#include "esp_err.h"
#include "app_nvs.h"
#include "mqtt_client.h"
#include <stdbool.h>


#ifdef __cplusplus
extern "C"{
#endif

/**
 * @brief   Khởi tạo và kích hoạt kết nối MQTT Client dựa trên cấu hình thiết bị.
 * @param   pDeviceConfig Con trỏ chứa cấu hình thiết bị tải từ NVS.
 * @return  esp_err_t ESP_OK nếu thành phần thành công, hoặc mã lỗi chuyên sâu ngược lại.
 */
esp_err_t app_mqtt_StartInit(app_nvs_device_config_t *pDeviceConfig); 

/**
 * @brief   Lấy con trỏ quản lý client MQTT hiện tại.
 * @return  esp_mqtt_client_handle_t Con trỏ đến handle của MQTT client.
 */
esp_mqtt_client_handle_t mqtt_app_GetClient(void);

/**
 * @brief   Lấy thông tin cấu hình thiết bị đang được MQTT module lưu trữ.
 * @return  const device_config_t* Con trỏ hằng trỏ tới cấu trúc cấu hình thiết bị.
 */
const app_nvs_device_config_t *mqtt_app_GetDeviceConfig(void);

/**
 * @brief   Kiểm tra trạng thái kết nối mạng MQTT hiện tại.
 * @return  true nếu đã kết nối thành công, ngược lại false.
 */
bool app_mqtt_IsConnected(void);

#ifdef __cplusplus
}
#endif